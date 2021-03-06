#include "../include/paracuber/cnftree.hpp"
#include "../include/paracuber/cnf.hpp"
#include "../include/paracuber/communicator.hpp"
#include "../include/paracuber/config.hpp"
#include "../include/paracuber/util.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace paracuber {
const size_t CNFTree::maxPathDepth = sizeof(CNFTree::Path) * 8 - 6;

CNFTree::CNFTree(LogPtr log,
                 CNF& rootCNF,
                 std::shared_ptr<Config> config,
                 int64_t originCNFId)
  : m_rootCNF(rootCNF)
  , m_config(config)
  , m_originCNFId(originCNFId)
  , m_logger(log->createLoggerMT(
      ("CNFTree of formula " + std::to_string(rootCNF.getOriginId()))))
{}
CNFTree::~CNFTree() {}

CNFTree::State
CNFTree::getState(Path p) const
{
  std::lock_guard lock(m_nodeMapMutex);
  assert(getDepth(p) < maxPathDepth);
  const Node* node = getNode(p);
  if(node)
    return node->state;
  return State::UnknownPath;
}

void
CNFTree::setStateFromLocal(Path p, State state)
{
  std::lock_guard lock(m_nodeMapMutex);
  Node* node = getNode(p);
  if(!node) {
    node =
      m_nodeMap.insert(std::make_pair(cleanupPath(p), std::make_unique<Node>()))
        .first->second.get();
  }

  State stateBefore = node->state;
  node->state = state;

  if(getDepth(p) == 0 && node->state != stateBefore) {
    m_rootStateChangedSignal(p, state);
    return;
  }

  m_rootCNF.insertResult(cleanupPath(p), state, DefaultUninitiatedPath);

  propagateUpwardsFrom(p);
}
void
CNFTree::setStateFromRemote(Path p, State state, int64_t remoteId)
{
  std::lock_guard lock(m_nodeMapMutex);
  Node* node = getNode(p);
  assert(node);
  assert(node->offloadedTo == remoteId);
  node->state = state;

  propagateUpwardsFrom(p);
}
void
CNFTree::insertNodeFromRemote(Path p, int64_t remoteId)
{
  std::lock_guard lock(m_nodeMapMutex);
  auto [it, inserted] =
    m_nodeMap.insert(std::make_pair(cleanupPath(p), std::make_unique<Node>()));
  Node* node = it->second.get();
  if(!inserted) {
    // Node is offloaded again to the same remote node. This can basically only
    // happen if a node went offline, sent a status update, went online again
    // immediately and received the same job as before. If this event is just
    // silently ignored and the receivedFrom field replaced, the result will
    // still be propagated upwards correctly.
    PARACUBER_LOG(m_logger, GlobalWarning)
      << "Receive path " << pathToStrNoAlloc(p)
      << " that was inserted previously from remote " << node->receivedFrom
      << " again! This time from " << remoteId
      << ". Setting receivedFrom to new remote and ignore this network-related "
         "error.";
  }
  node->receivedFrom = remoteId;
}
void
CNFTree::offloadNodeToRemote(Path p, int64_t remoteId)
{
  std::lock_guard lock(m_nodeMapMutex);
  Node* node = getNode(p);
  if(!node) {
    node =
      m_nodeMap.insert(std::make_pair(cleanupPath(p), std::make_unique<Node>()))
        .first->second.get();
  }
  assert(!node->isOffloaded());

  node->offloadedTo = remoteId;
  node->state = Working;
}
void
CNFTree::resetNode(Path p)
{
  std::lock_guard lock(m_nodeMapMutex);
  Node* node = getNode(p);
  assert(node);
  assert(node->isOffloaded());
  node->offloadedTo = 0;
  node->state = Unvisited;
}

CNFTree::Path
CNFTree::getTopmostAvailableParent(Path p) const
{
  std::lock_guard lock(m_nodeMapMutex);
  return getTopmostAvailableParentInner(p);
}
CNFTree::Path
CNFTree::getTopmostAvailableParentInner(Path p) const
{
  const Node* n = getNode(p);
  if(!n)
    return DefaultUninitiatedPath;

  if(getDepth(p) == 0)
    return p;

  Path parentPath = getParent(p);
  const Node* parentNode = getNode(parentPath);
  if(parentNode->requiresRemoteUpdate()) {
    return parentPath;
  }
  return getTopmostAvailableParentInner(parentPath);
}

void
CNFTree::propagateUpwardsFrom(Path p, Path sourcePath)
{
  // Starts at a node, but must immediately go to parent if called with invalid
  // source path. This is only the first call, from functions above.
  if(sourcePath == DefaultUninitiatedPath) {
    if(getDepth(p) == 0)
      return;

    {
      // The node that has received an update could directly require sending it
      // to the remote!
      Node* node = getNode(p);
      if(node->requiresRemoteUpdate() &&
         (node->state == SAT || node->state == UNSAT)) {
        sendNodeResultToSender(p, *node);
      }
    }

    sourcePath = p;
    p = getParent(p);
  }

  Node* node = getNode(p);
  if(!node) {
    assert(getDepth(p) < maxPathDepth);
    // The node must always exist, except work was directly offloaded.
    assert(
      (getNode(sourcePath) && getNode(sourcePath)->requiresRemoteUpdate()));
    return;
  }

  bool changed = false;

  const Node* leftChild = getNode(getNextLeftPath(p));
  const Node* rightChild = getNode(getNextRightPath(p));

  if(leftChild && rightChild) {
    if(leftChild->state == UNSAT && rightChild->state == UNSAT) {
      node->state = UNSAT;
      changed = true;
    }

    if(leftChild->state == SAT || rightChild->state == SAT) {
      node->state = SAT;
      changed = true;
    }
  }

  if(changed) {
    setCNFResult(cleanupPath(p), node->state, sourcePath);

    if(getDepth(p) == 0) {
      m_rootStateChangedSignal(cleanupPath(p), node->state);
      return;
    }

    if(node->requiresRemoteUpdate()) {
      sendNodeResultToSender(p, *node);
    } else {
      propagateUpwardsFrom(getParent(p), p);
    }
  }
}

void
CNFTree::sendNodeResultToSender(Path p, const Node& node)
{
  assert(node.requiresRemoteUpdate());
  try {
    auto& statNode =
      m_config->getCommunicator()->getClusterStatistics()->getNode(
        node.receivedFrom);
    m_rootCNF.sendResult(statNode.getNetworkedNode(), cleanupPath(p), []() {});
  } catch(const std::invalid_argument& e) {
    // This case should be handled by other compute nodes detecting the
    // offline node. They re-insert missed tasks.
  }
}

void
CNFTree::pathToStr(Path p, char* str)
{
  // TODO: Make this more efficient if it is required.
  for(size_t i = 0; i < maxPathDepth; ++i) {
    str[i] = getAssignment(p, i + 1) + '0';
  }
  str[getDepth(p)] = '\0';
}
const char*
CNFTree::pathToStrNoAlloc(Path p)
{
  if(p == DefaultUninitiatedPath)
    return "(nowhere)";
  if(getDepth(p) > maxPathDepth)
    return "INVALID PATH";
  static thread_local char arr[maxPathDepth];
  pathToStr(p, arr);
  return arr;
}

std::string
CNFTree::pathToStdString(Path p)
{
  char str[maxPathDepth + 1];
  pathToStr(p, str);
  return (std::string(str, getDepth(p)) + " (" + std::to_string(getDepth(p)) +
          ")");
}

void
CNFTree::dumpTreeToFile(const std::string_view& file)
{
  std::ofstream outFile;
  outFile.open(std::string(file));
  if(outFile.is_open()) {
    outFile << "digraph ParaCuberTree {";
    bool limitedDump = m_config->isLimitedTreeDumpActive();
    std::string rootParentPath = "";
    std::set<Path> visitedPaths;
    for(const auto& it : m_nodeMap) {
      dumpNode(it.first,
               it.second.get(),
               visitedPaths,
               outFile,
               limitedDump,
               rootParentPath);
    }
    outFile << "}" << std::endl;
    outFile.close();
  }
}
void
CNFTree::dumpNode(Path p,
                  const Node* n,
                  PathSet& visitedPaths,
                  std::ostream& o,
                  bool limitedDump,
                  const std::string& parentPath)
{
  if(!n || !visitedPaths.insert(cleanupPath(p)).second)
    return;

  if(limitedDump && (n->state == SAT || n->state == UNSAT))
    return;

  std::string pStr = "n";
  pStr += CNFTree::pathToStrNoAlloc(p);
  o << pStr << " [label=\"" << pStr << "(" << n->state << ") >"
    << n->receivedFrom << ">" << n->offloadedTo << "\" shape=box];"
    << std::endl;

  if(parentPath != "") {
    o << parentPath << " -> " << pStr << ";" << std::endl;
  }

  {
    Path left = getNextLeftPath(p);
    dumpNode(left, getNode(left), visitedPaths, o, limitedDump, pStr);
  }
  {
    Path right = getNextRightPath(p);
    dumpNode(right, getNode(right), visitedPaths, o, limitedDump, pStr);
  }
}

void
CNFTree::setCNFResult(Path p, State state, Path source)
{
  std::shared_ptr<CNF> rootCNF = GetRootCNF(m_config.get(), m_originCNFId);
  if(!rootCNF)
    return;
  rootCNF->insertResult(cleanupPath(p), state, cleanupPath(source));
}

CNFTree::Path
CNFTree::strToPath(const char* str, size_t len)
{
  Path p = CNFTree::setDepth(0, len);
  for(size_t i = 0; i < len; ++i) {
    if(str[i] == '0') {
      p = setAssignment(p, i + 1, false);
    } else {
      p = setAssignment(p, i + 1, true);
    }
  }
  return p;
}
std::ostream&
operator<<(std::ostream& o, CNFTree::State s)
{
  o << CNFTreeStateToStr(s);
  return o;
}
}

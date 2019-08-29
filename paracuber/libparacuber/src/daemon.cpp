#include "../include/paracuber/daemon.hpp"
#include "../include/paracuber/communicator.hpp"
#include "../include/paracuber/config.hpp"
#include "../include/paracuber/log.hpp"

namespace paracuber {
Daemon::Context::Context(std::shared_ptr<CNF> rootCNF,
                         int64_t originatorID,
                         uint32_t cnfVarCount,
                         Daemon* daemon,
                         ClusterStatistics::Node& statsNode)
  : m_rootCNF(rootCNF)
  , m_originatorID(originatorID)
  , m_cnfVarCount(cnfVarCount)
  , m_daemon(daemon)
  , m_logger(daemon->m_log->createLogger())
  , m_statisticsNode(statsNode)
{
  PARACUBER_LOG(m_logger, Trace)
    << "Create new context with origin " << m_originatorID
    << " and a CNF variable count of " << m_cnfVarCount;
}
Daemon::Context::~Context()
{
  PARACUBER_LOG(m_logger, Trace)
    << "Destroy context with origin " << m_originatorID;
}

Daemon::Daemon(ConfigPtr config,
               LogPtr log,
               std::shared_ptr<Communicator> communicator)
  : m_config(config)
  , m_log(log)
  , m_communicator(communicator)
{
  m_config->m_daemon = this;
}

std::pair<Daemon::Context&, bool>
Daemon::getOrCreateContext(std::shared_ptr<CNF> rootCNF,
                           int64_t id,
                           uint32_t varCount)
{
  if(m_contextMap.count(id) > 0) {
    return { *m_contextMap.find(id)->second, false };
  } else {
    auto [statsNode, inserted] =
      m_communicator->getClusterStatistics()->getOrCreateNode(id);

    auto p = std::make_pair(
      id, std::make_unique<Context>(rootCNF, id, varCount, this, statsNode));
    Context& context = *p.second;
    m_contextMap.insert(std::move(p));
    return { context, true };
  }
}

Daemon::~Daemon() {}
}

@0xead81247730f0294;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("paracuber::message");

struct DaemonContext
{
  originator @0 : Int64; # Id of originator node
  state @1 : UInt8; # State of the context (taken directly from C++ enum)
  factorySize @2 : UInt64; # Size of the factory queue in this context
}
# A structure describing the state of a daemon.

struct Daemon
{
  contexts @0 : List(DaemonContext); # List of all contexts on this node
}

struct Node
{
  name @0 : Text; # Name of the node
  id @1 : Int64; # Id of the node
  availableWorkers @2 : UInt16; # number of available worker threads on the node
  workQueueCapacity @3 : UInt64; # number of tasks the work queue can hold
  workQueueSize @4 : UInt64; # number of tasks currently in the work queue, like in the status message
  maximumCPUFrequency @5 : UInt16; # maximum frequency of the cpu on this node
  uptime @6 : UInt32; # uptime (in seconds) of the node
  udpListenPort @7 : UInt16; # listen port for udp control messages
  tcpListenPort @8 : UInt16; # listen port for tcp data messages
  daemonMode @9 : Bool; # determines if the node is in daemon or client mode
}


struct AnnouncementRequest
{
  requester @0 : Node;

  nameMatch : union {
    noRestriction @1 : Void;
    regex @2 : Text;
    id @3: Int64;
  }
}
# Request announcements of online nodes with given
# restrictions. Announcement should be sent per unicast.

struct OnlineAnnouncement
{
  node @0 : Node;
}
# Announce a node is online. May be sent regularily or
# after issuing an AnnouncementRequest.

struct OfflineAnnouncement
{
  reason @0 : Text;
}
# Announce a node is coming down and going offline.

struct NodeStatus
{
  workQueueSize @0 : UInt64; # number of tasks in worker queue
  union {
    client @1 : Void;
    daemon @2 : Daemon;
  }
}
# An update about node statistics. Sent to all other
# known online nodes approximately every second.

struct CNFTreeNodeStatusRequest
{
  handle @0 : Int64;
  path @1 : UInt64;
  cnfId @2 : Int64;
}

struct CNFTreeNodeStatusReplyEntry
{
  literal @0 : Int32;
  state @1 : UInt8;
}

struct CNFTreeNodeStatusReply
{
  handle @0 : Int64;
  path @1 : UInt64;
  cnfId @2 : Int64;
  nodes @3 : List(CNFTreeNodeStatusReplyEntry);
}

struct Message
{
  id @0 : Int16;
  # Message ID, starting at - 2^15 and increasing up to 2 ^ 15 - 1, then wrapping around
  origin @1 : Int64;
  # Id of the sender. Can be generated by using PID + MAC Address or PID (16 Bit) + 48 Bit Unique Number.

  union
  {
    announcementRequest @2 : AnnouncementRequest;
    onlineAnnouncement @3 : OnlineAnnouncement;
    offlineAnnouncement @4 : OfflineAnnouncement;
    nodeStatus @5 : NodeStatus;
    cnfTreeNodeStatusRequest @6 : CNFTreeNodeStatusRequest;
    cnfTreeNodeStatusReply @7 : CNFTreeNodeStatusReply;
  }
  # acual message body
}

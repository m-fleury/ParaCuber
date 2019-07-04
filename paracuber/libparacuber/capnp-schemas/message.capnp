@0xead81247730f0294;

struct AnnouncementRequest
{
  nameMatch : union {
    noRestriction @0 : Void;
    regex @1 : Text;
  }
}
# Request announcements of online nodes with given
# restrictions. Announcement should be sent per unicast.

struct OnlineAnnouncement
{
  name @0 : Text; # Name of the node
  availableWorkers @1 : UInt16; # number of available worker threads on the node
  workerQueueCapacity @2 : UInt64; # number of tasks the worker queue can hold
  maximumCPUFrequency @3 : UInt16; # maximum frequency of the cpu on this node
  uptime @4 : UInt32; # uptime (in seconds) of the node
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
  workerQueueSize @0 : UInt64; # number of tasks in worker queue
}
# An update about node statistics. Sent to all other
# known online nodes approximately every second.


struct Message
{
  id @0 : Int16;
  # Message ID, starting at - 2^15 and increasing up to 2 ^ 15 - 1, then wrapping around
  origin @1 : Int64;
  # Id of the sender. Can be generated by using PID + MAC Address.

  union
  {
    announcementRequest @2 : AnnouncementRequest;
    onlineAnnouncement @3 : OnlineAnnouncement;
    offlineAnnouncement @4 : OfflineAnnouncement;
    nodeStatus @5 : NodeStatus;
  }
  # acual message body
}

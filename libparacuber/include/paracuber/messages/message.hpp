#ifndef PARACUBER_MESSAGES_BASE
#define PARACUBER_MESSAGES_BASE

#include <cereal/access.hpp>
#include <cereal/types/variant.hpp>
#include <cstdint>
#include <variant>

#include "announcement_request.hpp"
#include "cnftree_node_status_reply.hpp"
#include "cnftree_node_status_request.hpp"
#include "node_status.hpp"
#include "offline_announcement.hpp"
#include "online_announcement.hpp"
#include "type.hpp"

#define PARACUBER_MESSAGES_MESSAGE_GETSET_BODY(NAME)             \
  const NAME& get##NAME() const { return std::get<NAME>(body); } \
  Message insert##NAME(const NAME& val)                          \
  {                                                              \
    body = val;                                                  \
    return *this;                                                \
  }                                                              \
  Message insert##NAME(const NAME&& val)                         \
  {                                                              \
    body = std::move(val);                                       \
    return *this;                                                \
  }                                                              \
  Message insert(const NAME& val)                                \
  {                                                              \
    body = val;                                                  \
    return *this;                                                \
  }                                                              \
  Message insert(const NAME&& val)                               \
  {                                                              \
    body = std::move(val);                                       \
    return *this;                                                \
  }

namespace paracuber {
namespace messages {
class Message
{
  public:
  Message() {}
  Message(int64_t origin, int64_t target = 0)
    : origin(origin)
    , target(target)
  {}
  virtual ~Message() {}

  Type getType() const
  {
    if(std::holds_alternative<OnlineAnnouncement>(body))
      return Type::OnlineAnnouncement;
    if(std::holds_alternative<OfflineAnnouncement>(body))
      return Type::OfflineAnnouncement;
    if(std::holds_alternative<AnnouncementRequest>(body))
      return Type::AnnouncementRequest;
    if(std::holds_alternative<NodeStatus>(body))
      return Type::NodeStatus;
    if(std::holds_alternative<CNFTreeNodeStatusRequest>(body))
      return Type::CNFTreeNodeStatusRequest;
    if(std::holds_alternative<CNFTreeNodeStatusReply>(body))
      return Type::CNFTreeNodeStatusReply;

    return Type::Unknown;
  }
  const int64_t getOrigin() const { return origin; }

  PARACUBER_MESSAGES_MESSAGE_GETSET_BODY(AnnouncementRequest)
  PARACUBER_MESSAGES_MESSAGE_GETSET_BODY(OnlineAnnouncement)
  PARACUBER_MESSAGES_MESSAGE_GETSET_BODY(OfflineAnnouncement)
  PARACUBER_MESSAGES_MESSAGE_GETSET_BODY(NodeStatus)
  PARACUBER_MESSAGES_MESSAGE_GETSET_BODY(CNFTreeNodeStatusRequest)
  PARACUBER_MESSAGES_MESSAGE_GETSET_BODY(CNFTreeNodeStatusReply)

  using MessageBodyVariant = std::variant<NodeStatus,
                                          OnlineAnnouncement,
                                          OfflineAnnouncement,
                                          AnnouncementRequest,
                                          CNFTreeNodeStatusRequest,
                                          CNFTreeNodeStatusReply>;

  int64_t getTarget() const { return target; }

  private:
  friend class cereal::access;

  int64_t origin;
  MessageBodyVariant body;
  int64_t target = 0;

  template<class Archive>
  void serialize(Archive& ar)
  {
    ar(CEREAL_NVP(origin), CEREAL_NVP(target), CEREAL_NVP(body));
  }
};

}
}

// CEREAL_REGISTER_TYPE(paracuber::messages::Message);

#endif

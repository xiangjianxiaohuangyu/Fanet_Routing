/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "qgpsr_packet.h"
#include "ns3/address-utils.h"
#include "ns3/packet.h"
#include "ns3/log.h"

NS_LOG_COMPONENT_DEFINE("QgpsrPacket");
/**
 * Helper functions to serialize floating point numbers
 *
 * Float is stored within an union, and the uint32_t (4-byte) representation of the stored bytes can be gathered.
 * This can then be serialized with the appropriate existing method.
 *
 */
static uint32_t
FtoU32(float v)
{
  /**
   * Converts a float to an uint32_t binary representation.
   * \param v The float value
   * \returns The uint32_t binary representation
   */
  union Handler
  {
    float f;
    uint32_t b;
  };

  Handler h;
  h.f = v;
  return h.b;
}

static float
U32toF(uint32_t v)
{
  /**
   * Converts an uint32_t binary representation to a float.
   * \param v The uint32_t binary representation
   * \returns The float value.
   */
  union Handler
  {
    float f;
    uint32_t b;
  };

  Handler h;
  h.b = v;
  return h.f;
}
namespace ns3
{
  namespace qgpsr
  {

    NS_OBJECT_ENSURE_REGISTERED(TypeHeader);

    TypeHeader::TypeHeader(MessageType t = GPSRTYPE_HELLO)
        : m_type(t),
          m_valid(true)
    {
    }

    TypeId
    TypeHeader::GetTypeId()
    {
      static TypeId tid = TypeId("ns3::qgpsr::TypeHeader")
                              .SetParent<Header>()
                              .AddConstructor<TypeHeader>();
      return tid;
    }

    TypeId
    TypeHeader::GetInstanceTypeId() const
    {
      return GetTypeId();
    }

    uint32_t
    TypeHeader::GetSerializedSize() const
    {
      return 1;
    }

    void
    TypeHeader::Serialize(Buffer::Iterator i) const
    {
      i.WriteU8((uint8_t)m_type);
    }

    uint32_t
    TypeHeader::Deserialize(Buffer::Iterator start)
    {
      Buffer::Iterator i = start;
      uint8_t type = i.ReadU8();
      m_valid = true;
      switch (type)
      {
      case GPSRTYPE_HELLO:
      case GPSRTYPE_POS:
      {
        m_type = (MessageType)type;
        break;
      }
      default:
        m_valid = false;
      }
      uint32_t dist = i.GetDistanceFrom(start);
      NS_ASSERT(dist == GetSerializedSize());
      return dist;
    }

    void
    TypeHeader::Print(std::ostream &os) const
    {
      switch (m_type)
      {
      case GPSRTYPE_HELLO:
      {
        os << "HELLO";
        break;
      }
      case GPSRTYPE_POS:
      {
        os << "POSITION";
        break;
      }
      default:
        os << "UNKNOWN_TYPE";
      }
    }

    bool
    TypeHeader::operator==(TypeHeader const &o) const
    {
      return (m_type == o.m_type && m_valid == o.m_valid);
    }

    std::ostream &
    operator<<(std::ostream &os, TypeHeader const &h)
    {
      h.Print(os);
      return os;
    }

    //-----------------------------------------------------------------------------
    // HELLO
    //-----------------------------------------------------------------------------
    HelloHeader::HelloHeader(uint64_t originPosx, uint64_t originPosy)
        : m_originPosx(originPosx),
          m_originPosy(originPosy),
          m_originPosz(0)
    {
    }
    HelloHeader::HelloHeader(uint64_t originPosx, uint64_t originPosy, uint64_t originPosz, std::map<Ipv4Address, float> max_qvalue, Ipv4Address destination)
        : m_originPosx(originPosx),
          m_originPosy(originPosy),
          m_originPosz(originPosz),
          m_max_qvalue(max_qvalue),
          m_dst(destination)
    {
      map_size = m_max_qvalue.size();
    }

    NS_OBJECT_ENSURE_REGISTERED(HelloHeader);

    TypeId
    HelloHeader::GetTypeId()
    {
      static TypeId tid = TypeId("ns3::qgpsr::HelloHeader")
                              .SetParent<Header>()
                              .AddConstructor<HelloHeader>();
      return tid;
    }

    TypeId
    HelloHeader::GetInstanceTypeId() const
    {
      return GetTypeId();
    }

    uint32_t
    HelloHeader::GetSerializedSize() const
    {
      return (12 + map_size * 8 + 1 + 4);
    }

    void
    HelloHeader::Serialize(Buffer::Iterator i) const
    {
      NS_LOG_DEBUG("Serialize X " << m_originPosx << " Y " << m_originPosy << "Z" << m_originPosz);

      i.WriteHtonU32(m_originPosx);
      i.WriteHtonU32(m_originPosy);
      i.WriteHtonU32(m_originPosz);
      i.WriteU8(map_size);

      for (std::map<Ipv4Address, float>::const_iterator j = m_max_qvalue.begin(); j != m_max_qvalue.end(); j++)
      {
        WriteTo(i, j->first);
        i.WriteHtonU32(FtoU32((float)j->second));
      }
      WriteTo(i, m_dst);
    }

    uint32_t
    HelloHeader::Deserialize(Buffer::Iterator start)
    {

      Buffer::Iterator i = start;

      m_originPosx = i.ReadNtohU32();
      m_originPosy = i.ReadNtohU32();
      m_originPosz = i.ReadNtohU32();
      map_size = i.ReadU8();

      for (size_t j = 0; j < map_size; j++)
      {
        Ipv4Address a;
        ReadFrom(i, a);
        float b = U32toF(i.ReadNtohU32());
        m_max_qvalue[a] = b;
      }
      ReadFrom(i, m_dst);

      NS_LOG_DEBUG("Deserialize X " << m_originPosx << " Y " << m_originPosy << "Z" << m_originPosz);

      uint32_t dist = i.GetDistanceFrom(start);
      NS_ASSERT_MSG(dist == (12 + static_cast<uint32_t>(map_size) * 8 + 1 + 4), "解序列化的字节大小不对！");
      return dist;
    }

    void
    HelloHeader::Print(std::ostream &os) const
    {
      os << " PositionX: " << m_originPosx
         << " PositionY: " << m_originPosy
         << " PositionZ: " << m_originPosz << "\n"
         << " map_size:" << map_size << "\n";
      for (const auto &i : m_max_qvalue)
      {
        os << "目的节点：" << i.first << "最大Q值:" << i.second << std::endl;
      }
    }

    std::ostream &
    operator<<(std::ostream &os, HelloHeader const &h) /* 全局函数重载 */
    {
      h.Print(os);
      return os;
    }

    bool
    HelloHeader::operator==(HelloHeader const &o) const /* 成员函数重载 */
    {
      return (m_originPosx == o.m_originPosx && m_originPosy == o.m_originPosy);
    }

    //-----------------------------------------------------------------------------
    // Position
    //-----------------------------------------------------------------------------
    PositionHeader::PositionHeader(uint64_t dstPosx, uint64_t dstPosy, uint32_t updated, uint64_t recPosx, uint64_t recPosy, uint8_t inRec, uint64_t lastPosx, uint64_t lastPosy)
        : m_dstPosx(dstPosx),
          m_dstPosy(dstPosy),
          m_dstPosz(0),
          m_updated(updated),
          m_recPosx(recPosx),
          m_recPosy(recPosy),
          m_recPosz(0),
          m_inRec(inRec),
          m_lastPosx(lastPosx),
          m_lastPosy(lastPosy),
          m_lastPosz(0)
    {
    }
    /* PositionHeader::PositionHeader(uint64_t dstPosx, uint64_t dstPosy, uint64_t dstPosz, uint32_t updated, uint64_t recPosx, uint64_t recPosy, uint64_t recPosz, uint8_t inRec,
                                   uint64_t lastPosx, uint64_t lastPosy, uint64_t lastPosz)
        : m_dstPosx(dstPosx),
          m_dstPosy(dstPosy),
          m_dstPosz(dstPosz),
          m_updated(updated),
          m_recPosx(recPosx),
          m_recPosy(recPosy),
          m_recPosz(recPosz),
          m_inRec(inRec),
          m_lastPosx(lastPosx),
          m_lastPosy(lastPosy),
          m_lastPosz(lastPosz)
    {
    } */
    /* add prehop  */
    PositionHeader::PositionHeader(uint64_t dstPosx, uint64_t dstPosy, uint64_t dstPosz, Ipv4Address prehop, uint32_t updated, uint64_t recPosx, uint64_t recPosy, uint64_t recPosz, uint8_t inRec,
                                   uint64_t lastPosx, uint64_t lastPosy, uint64_t lastPosz)
        : m_dstPosx(dstPosx),
          m_dstPosy(dstPosy),
          m_dstPosz(dstPosz),
          m_prehop(prehop),
          m_updated(updated),
          m_recPosx(recPosx),
          m_recPosy(recPosy),
          m_recPosz(recPosz),
          m_inRec(inRec),
          m_lastPosx(lastPosx),
          m_lastPosy(lastPosy),
          m_lastPosz(lastPosz)
    {
    }

    NS_OBJECT_ENSURE_REGISTERED(PositionHeader);

    TypeId
    PositionHeader::GetTypeId()
    {
      static TypeId tid = TypeId("ns3::qgpsr::PositionHeader")
                              .SetParent<Header>()
                              .AddConstructor<PositionHeader>();
      return tid;
    }

    TypeId
    PositionHeader::GetInstanceTypeId() const
    {
      return GetTypeId();
    }
    /* 序列化的大小是多少个字节 */
    uint32_t
    PositionHeader::GetSerializedSize() const
    {
      return 81;
    }

    void
    PositionHeader::Serialize(Buffer::Iterator i) const
    {
      i.WriteU64(m_dstPosx);
      i.WriteU64(m_dstPosy);
      i.WriteU64(m_dstPosz);
      WriteTo(i, m_prehop);
      i.WriteU32(m_updated);
      i.WriteU64(m_recPosx);
      i.WriteU64(m_recPosy);
      i.WriteU64(m_recPosz);
      i.WriteU8(m_inRec);
      i.WriteU64(m_lastPosx);
      i.WriteU64(m_lastPosy);
      i.WriteU64(m_lastPosz);
    }

    uint32_t
    PositionHeader::Deserialize(Buffer::Iterator start)
    {
      Buffer::Iterator i = start;
      m_dstPosx = i.ReadU64();
      m_dstPosy = i.ReadU64();
      m_dstPosz = i.ReadU64();
      ReadFrom(i, m_prehop);
      m_updated = i.ReadU32();
      m_recPosx = i.ReadU64();
      m_recPosy = i.ReadU64();
      m_recPosz = i.ReadU64();
      m_inRec = i.ReadU8();
      m_lastPosx = i.ReadU64();
      m_lastPosy = i.ReadU64();
      m_lastPosz = i.ReadU64();

      uint32_t dist = i.GetDistanceFrom(start);
      NS_ASSERT(dist == GetSerializedSize());
      return dist;
    }

    void
    PositionHeader::Print(std::ostream &os) const
    {
      os << " PositionX: " << m_dstPosx
         << " PositionY: " << m_dstPosy
         << " PositionZ: " << m_dstPosz
         << " prehop: " << m_prehop
         << " Updated: " << m_updated
         << " RecPositionX: " << m_recPosx
         << " RecPositionY: " << m_recPosy
         << " RecPositionY: " << m_recPosz
         << " inRec: " << m_inRec
         << " LastPositionX: " << m_lastPosx
         << " LastPositionY: " << m_lastPosy
         << " LastPositionZ: " << m_lastPosz;
    }

    std::ostream &
    operator<<(std::ostream &os, PositionHeader const &h)
    {
      h.Print(os);
      return os;
    }

    bool
    PositionHeader::operator==(PositionHeader const &o) const
    {
      return (m_dstPosx == o.m_dstPosx && m_dstPosy == o.m_dstPosy && m_updated == o.m_updated && m_recPosx == o.m_recPosx && m_recPosy == o.m_recPosy && m_inRec == o.m_inRec && m_lastPosx == o.m_lastPosx && m_lastPosy == o.m_lastPosy);
    }

  }
}

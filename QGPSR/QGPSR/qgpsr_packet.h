/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#ifndef GPSRPACKET_H
#define GPSRPACKET_H

#include <iostream>
#include "ns3/header.h"
#include "ns3/enum.h"
#include "ns3/ipv4-address.h"
#include <map>
#include "ns3/nstime.h"
#include "ns3/vector.h"

namespace ns3
{
  namespace qgpsr
  {

    enum MessageType
    {
      GPSRTYPE_HELLO = 1, //!< GPSRTYPE_HELLO
      GPSRTYPE_POS = 2,   //!< GPSRTYPE_POS
    };

    /**
     * \ingroup gpsr
     * \brief GPSR types
     */
    /*
|--------------*--------------|
|    m_type    |   m_valid    |     hello消息和pos消息的公共头部
|--------------*--------------|
*/
    class TypeHeader : public Header
    /*普通header*/
    {
    public:
      /// c-tor
      TypeHeader(MessageType t);

      ///\name Header serialization/deserialization
      //\{
      /*  序列化：将对象变成字节流的形式传出去。

        反序列化：从字节流恢复成原来的对象。*/
      static TypeId GetTypeId();
      TypeId GetInstanceTypeId() const;
      uint32_t GetSerializedSize() const;
      void Serialize(Buffer::Iterator start) const;
      uint32_t Deserialize(Buffer::Iterator start);
      void Print(std::ostream &os) const;
      //\}

      /// Return type
      MessageType Get() const
      {
        return m_type; // 返回hello数据包或者位置数据包
      }
      /// Check that type if valid
      bool IsValid() const
      {
        return m_valid; // FIXME that way it wont work
      }
      bool operator==(TypeHeader const &o) const; // 比较头部
    private:
      MessageType m_type;
      bool m_valid;
    };

    std::ostream &operator<<(std::ostream &os, TypeHeader const &h);

    /*hello类型的头部*/
    //-----------------------------------------------------------------------------
    // HELLO
    //-----------------------------------------------------------------------------

    /*
    *--------------*--------------*
    |    m_type    |   m_valid    |     hello消息和pos消息的公共头部
    *--------------*--------------*
    *--------------*--------------*
    |   originPosx |   originPosy |     hello消息头部
    *--------------*--------------*
    *--------------*--------------*
    |   num_of_des |   max_qvalue |    源节点中不同目的节点对应一个最大Q值
    *--------------*--------------*
    */
    class HelloHeader : public Header
    {
    public:
      /// c-tor
      HelloHeader(uint64_t originPosx = 0, uint64_t originPosy = 0);

      HelloHeader(uint64_t originPosx, uint64_t originPosy, uint64_t originPosz, std::map<Ipv4Address, float> max_qvalue, Ipv4Address destination); // 重载三维
      ///\name Header serialization/deserialization

      //\{
      static TypeId GetTypeId();
      TypeId GetInstanceTypeId() const;
      uint32_t GetSerializedSize() const;
      void Serialize(Buffer::Iterator start) const;
      uint32_t Deserialize(Buffer::Iterator start);
      void Print(std::ostream &os) const;

      void SetOriginPosx(uint64_t posx)
      {
        m_originPosx = posx;
      }
      uint64_t GetOriginPosx() const
      {
        return m_originPosx;
      }
      void SetOriginPosy(uint64_t posy)
      {
        m_originPosy = posy;
      }
      uint64_t GetOriginPosy() const
      {
        return m_originPosy;
      }
      void SetOriginPosz(uint64_t posz)
      {
        m_originPosz = posz;
      }
      uint64_t GetOriginPosz() const
      {
        return m_originPosz;
      }
      void SetM_max_qvalue(std::map<Ipv4Address, float> max_qvalue)
      {
        m_max_qvalue = max_qvalue;
      }
      Ipv4Address GetDst() const
      {
        return m_dst;
      }
      /* 获取<目的节点，最大Q值> */
      std::map<Ipv4Address, float> GetM_max_qvalue() const
      {
        return m_max_qvalue;
      }
      /* map_size只能获取，不能设置 */
      uint8_t GetMap_size() const
      {
        return map_size;
      }

      bool operator==(HelloHeader const &o) const;

    private:
      uint64_t m_originPosx;
      uint64_t m_originPosy;
      uint64_t m_originPosz;
      /* <目的节点，最大Q值> */
      std::map<Ipv4Address, float> m_max_qvalue;
      /* 目的节点数 */
      uint8_t map_size;
      /* 发送源的目的节点 */
      Ipv4Address m_dst;
    };

    std::ostream &operator<<(std::ostream &os, HelloHeader const &);

    /*PositionHeader类型的头部*/

    //-----------------------------------------------------------------------------
    // Position
    //-----------------------------------------------------------------------------

    /*
    *--------------*--------------*
    |    m_type    |   m_valid    |     hello消息和pos消息的公共头部
    *--------------*--------------*
    *--------------*--------------*
    |   dstPosx    |   dstPosy    |     pos消息头部
    *--------------*--------------*
    *--------------*--------------*
    |   updated    |   recPosx    |     检查节点是否更新了目标位置
    *--------------*--------------*
    *--------------*--------------*
    |   recPosy    |   inRec      |
    *--------------*--------------*
    *--------------*--------------*
    |   lastPosx   |   lastPosy   |
    *--------------*--------------*
    *--------------*--------------*
    |     ipv4数据数据包内容       |     这里放的是希望转发到目的节点的数据包内容
    *--------------*--------------*
    */
    //
    class PositionHeader : public Header
    {
    public:
      PositionHeader(uint64_t dstPosx = 0, uint64_t dstPosy = 0, uint32_t updated = 0, uint64_t recPosx = 0,
                     uint64_t recPosy = 0, uint8_t inRec = 0, uint64_t lastPosx = 0, uint64_t lastPosy = 0);

      PositionHeader(uint64_t dstPosx, uint64_t dstPosy, uint64_t dstPosz, uint32_t updated = 0, // 重载三维优化
                     uint64_t recPosx = 0, uint64_t recPosy = 0, uint64_t recPosz = 0, uint8_t inRec = 0,
                     uint64_t lastPosx = 0, uint64_t lastPosy = 0, uint64_t lastPosz = 0);
      PositionHeader(uint64_t dstPosx, uint64_t dstPosy, uint64_t dstPosz, Ipv4Address prehop, uint32_t updated = 0, // 重载三维优化
                     uint64_t recPosx = 0, uint64_t recPosy = 0, uint64_t recPosz = 0, uint8_t inRec = 0,
                     uint64_t lastPosx = 0, uint64_t lastPosy = 0, uint64_t lastPosz = 0);
      static TypeId GetTypeId();
      TypeId GetInstanceTypeId() const;
      uint32_t GetSerializedSize() const;
      void Serialize(Buffer::Iterator start) const;
      uint32_t Deserialize(Buffer::Iterator start);
      void Print(std::ostream &os) const;

      void SetDstPosx(uint64_t posx)
      {
        m_dstPosx = posx;
      }
      uint64_t GetDstPosx() const
      {
        return m_dstPosx;
      }
      void SetDstPosy(uint64_t posy)
      {
        m_dstPosy = posy;
      }
      uint64_t GetDstPosy() const
      {
        return m_dstPosy;
      }
      void SetDstPosz(uint64_t posz)
      {
        m_dstPosz = posz;
      }
      uint64_t GetDstPosz() const
      {
        return m_dstPosz;
      }
      void SetUpdated(uint32_t updated)
      {
        m_updated = updated;
      }
      uint32_t GetUpdated() const
      {
        return m_updated;
      }
      void SetRecPosx(uint64_t posx)
      {
        m_recPosx = posx;
      }
      uint64_t GetRecPosx() const
      {
        return m_recPosx;
      }
      void SetRecPosy(uint64_t posy)
      {
        m_recPosy = posy;
      }
      uint64_t GetRecPosy() const
      {
        return m_recPosy;
      }
      void SetRecPosz(uint64_t posz)
      {
        m_recPosz = posz;
      }
      uint64_t GetRecPosz() const
      {
        return m_recPosz;
      }
      void SetInRec(uint8_t rec)
      {
        m_inRec = rec;
      }
      uint8_t GetInRec() const
      {
        return m_inRec;
      }
      void SetLastPosx(uint64_t posx)
      {
        m_lastPosx = posx;
      }
      uint64_t GetLastPosx() const
      {
        return m_lastPosx;
      }
      void SetLastPosy(uint64_t posy)
      {
        m_lastPosy = posy;
      }
      uint64_t GetLastPosy() const
      {
        return m_lastPosy;
      }
      void SetLastPosz(uint64_t posz)
      {
        m_lastPosz = posz;
      }
      uint64_t GetLastPosz() const
      {
        return m_lastPosz;
      }
      Ipv4Address GetPrehop() const
      {
        return m_prehop;
      }
      void SetPrehop( Ipv4Address prehop)
      {
        m_prehop = prehop;
      }

      bool operator==(PositionHeader const &o) const;

    private:
      uint64_t m_dstPosx;   ///< Destination Position x 目标位置 x
      uint64_t m_dstPosy;   ///< Destination Position x 目标位置 y
      uint64_t m_dstPosz;   ///< Destination Position x 目标位置 z
      Ipv4Address m_prehop; /// 上一跳节点
      uint32_t m_updated;   ///< Time of last update  上次更新时间
      uint64_t m_recPosx;   ///< x of position that entered Recovery-mode 进入恢复模式的位置的 x
      uint64_t m_recPosy;   ///< y of position that entered Recovery-mode 进入恢复模式的位置的 y
      uint64_t m_recPosz;   ///< y of position that entered Recovery-mode 进入恢复模式的位置的 z
      uint8_t m_inRec;      ///< 1 if in Recovery-mode, 0 otherwise
      uint64_t m_lastPosx;  ///< x of position of previous hop 前一跳位置的 x
      uint64_t m_lastPosy;  ///< y of position of previous hop 前一跳位置的 y
      uint64_t m_lastPosz;  ///< y of position of previous hop 前一跳位置的 z
    };

    std::ostream &operator<<(std::ostream &os, PositionHeader const &);

  }
}
#endif /* GPSRPACKET_H */

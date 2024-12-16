/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#define NS_LOG_APPEND_CONTEXT                                            \
  if (m_ipv4)                                                            \
  {                                                                      \
    std::clog << "[node " << m_ipv4->GetObject<Node>()->GetId() << "] "; \
  }

#include "qgpsr.h"
#include "ns3/log.h"
#include "ns3/boolean.h"
#include "ns3/random-variable-stream.h"
#include "ns3/inet-socket-address.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-net-device.h"
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/double.h"
#include <algorithm>
#include <limits>

#define GPSR_LS_GOD 0
// GPSR_LS_GOD算法使用源节点广播HELLO消息，并根据收到的HELLO消息计算链路质量和邻居节点信息，然后每个节点将链路状态信息发送给所有邻居节点。
#define GPSR_LS_RLS 1

namespace ns3
{
  NS_LOG_COMPONENT_DEFINE("QgpsrRoutingProtocol"); // 注册该文件为日志组件
  namespace qgpsr
  {
    struct backPacketTag : public Tag
    {
      uint8_t m_tag;
      backPacketTag() : Tag(),
                        m_tag(0){};
      static TypeId GetTypeId()
      {
        static TypeId tid = TypeId("ns3::qgpsr::backPacketTag").SetParent<Tag>();
        return tid;
      }

      TypeId GetInstanceTypeId() const
      {
        return GetTypeId();
      }

      uint32_t GetSerializedSize() const
      {
        return 1;
      }

      void Serialize(TagBuffer i) const
      {
        i.WriteU8(m_tag);
      }

      void Deserialize(TagBuffer i)
      {
        m_tag = i.ReadU8();
      }
      void Print(std::ostream &os) const
      {
      }
    };

    struct customTag : public Tag
    {
      // 从wifimac和socket中获取缓存队列
      uint32_t m_buff_queue;
      uint32_t m_sender;
      uint32_t m_receive;
      uint8_t m_delaytag;

      customTag(uint32_t neighborNumsOfNeighbor, uint32_t sender, uint32_t receive, uint8_t delaytag) : Tag(),
                                                                                                        m_buff_queue(neighborNumsOfNeighbor),
                                                                                                        m_sender(sender),
                                                                                                        m_receive(receive),
                                                                                                        m_delaytag(delaytag)

      {
      }
      customTag() : Tag(), /* 默认构造函数 */
                    m_buff_queue(0)
      {
      }
      static TypeId GetTypeId()
      {
        static TypeId tid = TypeId("ns3::qgpsr::customTag").SetParent<Tag>();
        return tid;
      }

      TypeId GetInstanceTypeId() const
      {
        return GetTypeId();
      }

      uint32_t GetSerializedSize() const
      {
        return 12 + 1; // 序列化大小固定在4个字节
      }

      void Serialize(TagBuffer i) const
      {
        i.WriteU32(m_buff_queue);
        i.WriteU32(m_sender);
        i.WriteU32(m_receive);
        i.WriteU8(m_delaytag);
      }

      void Deserialize(TagBuffer i)
      {
        m_buff_queue = i.ReadU32();
        m_sender = i.ReadU32();
        m_receive = i.ReadU32();
        m_delaytag = i.ReadU8();
      }
      void Print(std::ostream &os) const
      {
      }
    };

    struct DeferredRouteOutputTag : public Tag
    {
      /// Positive if output device is fixed in RouteOutput 如果输出设备在路由输出中固定，则为正
      uint32_t m_isCallFromL3;

      DeferredRouteOutputTag() : Tag(),
                                 m_isCallFromL3(0)
      {
      }

      static TypeId GetTypeId()
      {
        static TypeId tid = TypeId("ns3::qgpsr::DeferredRouteOutputTag").SetParent<Tag>();
        return tid;
      }

      TypeId GetInstanceTypeId() const
      {
        return GetTypeId();
      }

      uint32_t GetSerializedSize() const
      {
        return sizeof(uint32_t); // 序列化大小固定在4个字节
      }

      void Serialize(TagBuffer i) const
      {
        i.WriteU32(m_isCallFromL3);
      }

      void Deserialize(TagBuffer i)
      {
        m_isCallFromL3 = i.ReadU32();
      }

      void Print(std::ostream &os) const
      {
        os << "DeferredRouteOutputTag: m_isCallFromL3 = " << m_isCallFromL3;
      }
    };

/********** Miscellaneous constants 杂项常量**********/

/// Maximum allowed jitter.允许的最大抖动。
#define GPSR_MAXJITTER (HelloInterval.GetSeconds() / 2)
/// Random number between [(-GPSR_MAXJITTER)-GPSR_MAXJITTER] used to jitter HELLO packet transmission.
//[（-GPSR_MAXJITTER）-GPSR_MAXJITTER] 之间的随机数，用于抖动 HELLO 数据包传输。
#define JITTER (Seconds(UniformRandomVariable().GetValue(-GPSR_MAXJITTER, GPSR_MAXJITTER)))
#define FIRST_JITTER (Seconds(UniformRandomVariable().GetValue(0, GPSR_MAXJITTER))) // first Hello can not be in the past, used only on SetIpv4
    // 第一次 Hello 不能过去，只在 SetIpv4 上使用
    NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

    /// UDP Port for GPSR control traffic, not defined by IANA yet用于 GPSR 控制流量的 UDP 端口，尚未由 IANA 定义
    const uint32_t RoutingProtocol::GPSR_PORT = 666;

    RoutingProtocol::RoutingProtocol()
        : HelloInterval(Seconds(1)), // 修改hello消息间隔
          MaxQueueLen(64),
          MaxQueueTime(Seconds(30)),
          m_queue(MaxQueueLen, MaxQueueTime),
          HelloIntervalTimer(Timer::CANCEL_ON_DESTROY),
          PerimeterMode(false)
    {

      m_neighbors = PositionTable(); // 获取ptable.cc里面的PositionTable对象
    }

    TypeId
    RoutingProtocol::GetTypeId(void)
    {
      static TypeId tid = TypeId("ns3::qgpsr::RoutingProtocol")
                              .SetParent<Ipv4RoutingProtocol>()
                              .AddConstructor<RoutingProtocol>()
                              .AddAttribute("HelloInterval", "HELLO messages emission interval.",
                                            TimeValue(Seconds(1)),
                                            MakeTimeAccessor(&RoutingProtocol::HelloInterval),
                                            MakeTimeChecker())
                              .AddAttribute("LocationServiceName", "Indicates wich Location Service is enabled",
                                            EnumValue(GPSR_LS_GOD),
                                            MakeEnumAccessor(&RoutingProtocol::LocationServiceName),
                                            MakeEnumChecker(GPSR_LS_GOD, "GOD",
                                                            GPSR_LS_RLS, "RLS"))
                              .AddAttribute("PerimeterMode", "Indicates if PerimeterMode is enabled", // 指示是否启用了外围模式
                                            BooleanValue(false),
                                            MakeBooleanAccessor(&RoutingProtocol::PerimeterMode),
                                            MakeBooleanChecker());
      return tid;
    }

    RoutingProtocol::~RoutingProtocol()
    {
    }

    void
    RoutingProtocol::DoDispose()
    {
      m_ipv4 = 0;
      Ipv4RoutingProtocol::DoDispose();
    }

    Ptr<LocationService>
    RoutingProtocol::GetLS()
    {
      return m_locationService;
    }
    void
    RoutingProtocol::SetLS(Ptr<LocationService> locationService)
    {
      m_locationService = locationService;
    }
    // FIXME等下在这里获取当前节点
    /*  为从外部接收到的数据包选择下一跳。如果数据包的目的地址是本节点，则应将数据包传递给上层协议。否则，根据路由表选择下一跳。*/
    bool RoutingProtocol::RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                                     UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                                     LocalDeliverCallback lcb, ErrorCallback ecb)
    {

      NS_LOG_FUNCTION(this << p->GetUid() << header.GetDestination() << idev->GetAddress());
      if (m_socketAddresses.empty())
      {
        NS_LOG_LOGIC("No gpsr interfaces");
        return false;
      }
      NS_ASSERT(m_ipv4 != 0);
      NS_ASSERT(p != 0);
      // Check if input device supports IP
      NS_ASSERT(m_ipv4->GetInterfaceForDevice(idev) >= 0);

      int32_t iif = m_ipv4->GetInterfaceForDevice(idev);
      Ipv4Address dst = header.GetDestination();
      Ipv4Address origin = header.GetSource();

      DeferredRouteOutputTag tag;
      receivedNodesNum += 1;

      if (p->PeekPacketTag(tag) && IsMyOwnAddress(origin))
      {
        NS_LOG_UNCOND("进入队列");
        Ptr<Packet> packet = p->Copy();
        packet->RemovePacketTag(tag);
        DeferredRouteOutput(packet, header, ucb, ecb);
        return true;
      }

      if (m_ipv4->IsDestinationAddress(dst, iif)) // 确定接收到的数据包对应的地址和接口是否可以接受本地传送,
      // hello信息设置的除发送节点外，所有节点都是目的节点，所以这个判断下，hello和下一跳是目的节点的pos类型数据包会经过
      {

        Ptr<Packet> packet = p->Copy();
        TypeHeader tHeader(GPSRTYPE_POS);
        packet->RemoveHeader(tHeader);
        if (!tHeader.IsValid())
        {
          NS_LOG_DEBUG("GPSR message " << packet->GetUid() << " with unknown type received: " << tHeader.Get() << ". Ignored");
          return false;
        }

        if (tHeader.Get() == GPSRTYPE_POS)
        {
          
          PositionHeader phdr;
          packet->RemoveHeader(phdr);
        }

        if (dst != m_ipv4->GetAddress(1, 0).GetBroadcast())
        {
          NS_LOG_LOGIC("Unicast local delivery to " << dst); // 是单播转发
        }
        else
        {
          //          NS_LOG_LOGIC ("Broadcast local delivery to " << dst);
        }

        lcb(packet, header, iif);
        return true;
      }
      currentdst = dst;
      return Forwarding(p, header, ucb, ecb); // 转发
    }

    /* 数据包入队操作 */
    void
    RoutingProtocol::DeferredRouteOutput(Ptr<const Packet> p, const Ipv4Header &header,
                                         UnicastForwardCallback ucb, ErrorCallback ecb)
    {

      NS_LOG_FUNCTION(this << p << header);
      NS_ASSERT(p != 0 && p != Ptr<Packet>());

      if (m_queue.GetSize() == 0)
      {
        CheckQueueTimer.Cancel();
        CheckQueueTimer.Schedule(Time("500ms"));
      }

      QueueEntry newEntry(p, header, ucb, ecb);
      bool result = m_queue.Enqueue(newEntry);

      m_queuedAddresses.insert(m_queuedAddresses.begin(), header.GetDestination()); // 放入的是目的地址
      m_queuedAddresses.unique();

      if (result)
      {
        NS_LOG_LOGIC("Add packet " << p->GetUid() << " to queue. Protocol " << (uint16_t)header.GetProtocol());
      }
      NS_LOG_UNCOND("queued size:" << m_queue.GetSize());
    }

    void
    RoutingProtocol::CheckQueue()
    {

      CheckQueueTimer.Cancel();

      std::list<Ipv4Address> toRemove;

      for (std::list<Ipv4Address>::iterator i = m_queuedAddresses.begin(); i != m_queuedAddresses.end(); ++i)
      {
        if (SendPacketFromQueue(*i))
        {
          // Insert in a list to remove later
          toRemove.insert(toRemove.begin(), *i);
        }
      }

      // remove all that are on the list
      for (std::list<Ipv4Address>::iterator i = toRemove.begin(); i != toRemove.end(); ++i)
      {
        m_queuedAddresses.remove(*i);
      }

      if (!m_queuedAddresses.empty()) // Only need to schedule if the queue is not empty
      {
        CheckQueueTimer.Schedule(Time("500ms"));
      }
    }

    bool
    RoutingProtocol::SendPacketFromQueue(Ipv4Address dst)
    {
      NS_LOG_FUNCTION(this);
      NS_LOG_UNCOND("SendPacketFromQueue:" << dst);
      bool recovery = false;
      QueueEntry queueEntry;

      if (m_locationService->IsInSearch(dst))
      {
        return false;
      }

      if (!m_locationService->HasPosition(dst)) // Location-service stoped looking for the dst
      {
        m_queue.DropPacketWithDst(dst);
        NS_LOG_LOGIC("Location Service did not find dst. Drop packet to " << dst);
        return true;
      }

      Ptr<MobilityModel> MM = m_ipv4->GetObject<MobilityModel>();
      myPos.x = MM->GetPosition().x;
      myPos.y = MM->GetPosition().y;
      myPos.z = MM->GetPosition().z;

      Ipv4Address nextHop;

      if (m_neighbors.isNeighbour(dst))
      {
        nextHop = dst;
      }
      else
      {
        Vector dstPos = m_locationService->GetPosition(dst);
        std::set<Ipv4Address> myprenodes;                                /* 前一跳节点集 */
        std::vector<Ptr<Packet>> queuepackets = m_queue.GetPackets(dst); /* 获取排队数据包 */
        for (Ptr<Packet> prenode : queuepackets)
        {
          TypeHeader tHeader(GPSRTYPE_POS);
          prenode->RemoveHeader(tHeader);
          if (tHeader.Get() == GPSRTYPE_POS)
          {
            PositionHeader hdr;
            prenode->RemoveHeader(hdr);
            NS_LOG_UNCOND("延时排队数据包的前一跳节点:" << hdr.GetPrehop());
            myprenodes.insert(hdr.GetPrehop());
          }
        }
        nextHop = m_neighbors.QLGR(myprenodes, myPos, dstPos, dst);
        // NS_UNUSED(myprenodes);
        // nextHop = m_neighbors.BestNeighbor(dstPos, myPos);
        if (nextHop == Ipv4Address::GetZero())
        {
          NS_LOG_LOGIC("Fallback to recovery-mode. Packets to " << dst);
          recovery = true;
        }
        if (recovery) // 节点不存在下一跳，进入恢复模式
        {

          Vector Position;
          Vector previousHop;
          uint32_t updated;
          // 当前节点上的目的节点是dst的数据包，都要排队进行恢复模式
          while (m_queue.Dequeue(dst, queueEntry))
          {
            Ptr<Packet> p = ConstCast<Packet>(queueEntry.GetPacket());
            UnicastForwardCallback ucb = queueEntry.GetUnicastForwardCallback();
            Ipv4Header header = queueEntry.GetIpv4Header();

            TypeHeader tHeader(GPSRTYPE_POS);
            p->RemoveHeader(tHeader);
            if (!tHeader.IsValid())
            {
              NS_LOG_DEBUG("GPSR message " << p->GetUid() << " with unknown type received: " << tHeader.Get() << ". Drop");
              return false; // drop
            }
            if (tHeader.Get() == GPSRTYPE_POS)
            {
              PositionHeader hdr;
              p->RemoveHeader(hdr);
              Position.x = hdr.GetDstPosx();
              Position.y = hdr.GetDstPosy();
              Position.z = hdr.GetDstPosz();
              updated = hdr.GetUpdated();
            }

            PositionHeader posHeader(Position.x, Position.y, Position.z, m_selfIpv4Address, updated, myPos.x, myPos.y, myPos.z, (uint8_t)1, Position.x, Position.y, Position.z);
            p->AddHeader(posHeader); // 使用Dst节点的最后一个边缘位置进入恢复模式
            p->AddHeader(tHeader);

            RecoveryMode(dst, p, ucb, header); // 恢复模式中还是使用的是二维坐标
          }
          return true;
        }
      }
      Ptr<Ipv4Route> route = Create<Ipv4Route>();
      route->SetDestination(dst);
      route->SetGateway(nextHop);

      // FIXME: Does not work for multiple interfaces
      route->SetOutputDevice(m_ipv4->GetNetDevice(1));

      while (m_queue.Dequeue(dst, queueEntry)) // 把所有相同dst的数据包都发送到同一个下一跳节点
      {
        DeferredRouteOutputTag tag;
        Ptr<Packet> p = ConstCast<Packet>(queueEntry.GetPacket());

        UnicastForwardCallback ucb = queueEntry.GetUnicastForwardCallback();
        Ipv4Header header = queueEntry.GetIpv4Header();

        if (header.GetSource() == Ipv4Address("102.102.102.102"))
        {
          route->SetSource(m_ipv4->GetAddress(1, 0).GetLocal());
          header.SetSource(m_ipv4->GetAddress(1, 0).GetLocal());
        }
        else
        {
          route->SetSource(header.GetSource());
        }
        ucb(route, p, header);
      }
      return true;
    }

    /* 周边转发 */
    void
    RoutingProtocol::RecoveryMode(Ipv4Address dst, Ptr<Packet> p, UnicastForwardCallback ucb, Ipv4Header header)
    {

      Vector Position;
      Vector previousHop;
      uint32_t updated;
      uint32_t positionX;
      uint32_t positionY;
      uint32_t positionZ;
      Vector myPos;
      Vector recPos;

      Ptr<MobilityModel> MM = m_ipv4->GetObject<MobilityModel>();
      positionX = MM->GetPosition().x;
      positionY = MM->GetPosition().y;
      positionZ = MM->GetPosition().z;
      myPos.x = positionX;
      myPos.y = positionY;
      myPos.z = positionZ;

      TypeHeader tHeader(GPSRTYPE_POS);
      p->RemoveHeader(tHeader);
      if (!tHeader.IsValid())
      {
        NS_LOG_DEBUG("GPSR message " << p->GetUid() << " with unknown type received: " << tHeader.Get() << ". Drop");
        return; // drop
      }
      if (tHeader.Get() == GPSRTYPE_POS)
      {
        PositionHeader hdr;
        p->RemoveHeader(hdr);
        Position.x = hdr.GetDstPosx();
        Position.y = hdr.GetDstPosy();
        Position.z = hdr.GetDstPosz();
        updated = hdr.GetUpdated();
        recPos.x = hdr.GetRecPosx();
        recPos.y = hdr.GetRecPosy();
        recPos.z = hdr.GetRecPosz();
        previousHop.x = hdr.GetLastPosx();
        previousHop.y = hdr.GetLastPosy();
        previousHop.z = hdr.GetLastPosz();
      }

      PositionHeader posHeader(Position.x, Position.y, Position.z, m_selfIpv4Address, updated, recPos.x, recPos.y, recPos.z, (uint8_t)1, myPos.x, myPos.y, myPos.z);
      p->AddHeader(posHeader);
      p->AddHeader(tHeader);

      Ipv4Address nextHop = m_neighbors.BestAngle(previousHop, myPos);
      if (nextHop == Ipv4Address::GetZero())
      {
        return;
      }

      Ptr<Ipv4Route> route = Create<Ipv4Route>();
      route->SetDestination(dst);
      route->SetGateway(nextHop);

      route->SetOutputDevice(m_ipv4->GetNetDevice(1));
      route->SetSource(header.GetSource());

      NS_LOG_LOGIC(route->GetOutputDevice() << " forwarding in Recovery to " << dst << " through " << route->GetGateway() << " packet " << p->GetUid());
      ucb(route, p, header);
      return;
    }

    /*当网络接口状态变为“UP”时，调用此方法。自定义路由协议可以在此方法中执行与接口启用相关的操作。*/
    void
    RoutingProtocol::NotifyInterfaceUp(uint32_t interface)
    {
      NS_LOG_FUNCTION(this << m_ipv4->GetAddress(interface, 0).GetLocal());
      Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
      if (l3->GetNAddresses(interface) > 1)
      {
        NS_LOG_WARN("GPSR does not work with more then one address per each interface.");
      }
      Ipv4InterfaceAddress iface = l3->GetAddress(interface, 0);
      if (iface.GetLocal() == Ipv4Address("127.0.0.1"))
      {
        return;
      }

      // Create a socket to listen only on this interface创建套接字以仅侦听此接口
      Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(),
                                                UdpSocketFactory::GetTypeId());
      NS_ASSERT(socket != 0);
      socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvGPSR, this));

      socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), GPSR_PORT));
      socket->BindToNetDevice(l3->GetNetDevice(interface));
      socket->SetAllowBroadcast(true);
      socket->SetAttribute("IpTtl", UintegerValue(1));
      m_socketAddresses.insert(std::make_pair(socket, iface));

      // Allow neighbor manager use this interface for layer 2 feedback if possible
      Ptr<NetDevice> dev = m_ipv4->GetNetDevice(m_ipv4->GetInterfaceForAddress(iface.GetLocal()));
      Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice>();
      if (wifi == 0)
      {
        return;
      }
      Ptr<WifiMac> mac = wifi->GetMac();

      if (mac == 0)
      {
        return;
      }

      mac->TraceConnectWithoutContext("TxErrHeader", m_neighbors.GetTxErrorCallback());
    }

    void
    RoutingProtocol::RecvGPSR(Ptr<Socket> socket) /*接受到hello数据包然后更新邻居表*/
    {
      NS_LOG_FUNCTION(this << socket);
      // socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvGPSR, this));

      Address sourceAddress;
      Ptr<Packet> packet = socket->RecvFrom(sourceAddress); // 从socket检索发送者地址
      customTag tag1;
      packet->PeekPacketTag(tag1); // Tag机制来获取缓存队列数据

      // NS_LOG_UNCOND("tag:" << tag1.m_buff_queue);
      TypeHeader tHeader(GPSRTYPE_HELLO);
      packet->RemoveHeader(tHeader);
      if (!tHeader.IsValid())
      {
        NS_LOG_DEBUG("GPSR message " << packet->GetUid() << " with unknown type received: " << tHeader.Get() << ". Ignored");
        return;
      }

      receivedNodesNum += 1;
      HelloHeader hdr;
      packet->RemoveHeader(hdr);
      Vector Position;
      Position.x = hdr.GetOriginPosx();
      Position.y = hdr.GetOriginPosy();
      Position.z = hdr.GetOriginPosz();
      Ipv4Address neighborDst = hdr.GetDst();
      InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom(sourceAddress);
      Ipv4Address sender = inetSourceAddr.GetIpv4();
      Ipv4Address receiver = m_socketAddresses[socket].GetLocal();
      /* 增加表中条目 */
      m_neighbors.AddQtableEntry(sender, neighborDst); // 发送hello数据包源节点当时对应的dst
      /* 计算局部奖励 */

      float LR = 0;
      if (sender == currentdst && currentdst != Ipv4Address::GetZero())
      {
        LR = 0.2;
      }
      uint8_t mydelaytag = tag1.m_delaytag;//当前节点获取标签
      float senderOfN = (float)tag1.m_sender;
      float receiveOfN = (float)tag1.m_receive;
      uint32_t buffOfN = tag1.m_buff_queue;
      if (senderOfN == 0)
      {
        LR = (1 - alpha) * buffOfN;
      }
      else
      {
        LR = (1 - alpha) * buffOfN + alpha * receiveOfN / senderOfN;
      }
      Update_Qtable(hdr, sender, LR); // 更新最大邻居表和Qtable
      if (mydelaytag == 1)
      {
       m_neighbors.Update_GR_QTable(sender, neighborDst, -0.4);
      }

      UpdateRouteToNeighbor(sender, Position, receiver); // sender Position表示发出hello信息的节点ip和位置
    }

    void RoutingProtocol::Update_Qtable(HelloHeader &hdr, Ipv4Address sender, float LR)
    {
      std::map<Ipv4Address, float> max_qvalue = hdr.GetM_max_qvalue();
      Ipv4Address neighbordst = hdr.GetDst(); // hello消息传过来的属于邻居节点的目的地址

      for (auto i = max_qvalue.begin(); i != max_qvalue.end(); i++)
      {
        if (i->first != Ipv4Address("102.102.102.102") && i->first != Ipv4Address("0.0.0.0"))
        {
          m_neighbors.Update_VDQTable(sender, i->first, i->second);
        }
      }

      /* 更新当前节点的sender邻居节点的相关Q值 */
      m_neighbors.Update_LR_QTable(sender, neighbordst, LR);
    }

    /* 把发送信息的节点都作为接收信息节点的邻居节点 */
    // 更新邻居表也能更新缓存队列参数sender邻居、Pos邻居位置、当前节点receiver、缓存队列queue
    void RoutingProtocol::UpdateRouteToNeighbor(Ipv4Address sender, Vector Pos, Ipv4Address receiver)
    {
      NS_LOG_FUNCTION(this << "sender" << sender << "receiver" << receiver << Pos);

      m_neighbors.AddEntry(sender, Pos, receiver);
    }

    /*接口关闭时禁用相关的网络功能，并关闭数据包套接字Socket*/
    void
    RoutingProtocol::NotifyInterfaceDown(uint32_t interface)
    {
      NS_LOG_FUNCTION(this << m_ipv4->GetAddress(interface, 0).GetLocal());

      // Disable layer 2 link state monitoring (if possible)禁用第 2 层链路状态监控（如果可能）
      Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
      Ptr<NetDevice> dev = l3->GetNetDevice(interface);
      Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice>();
      if (wifi != 0)
      {
        Ptr<WifiMac> mac = wifi->GetMac()->GetObject<AdhocWifiMac>();

        if (mac != 0)
        {
          mac->TraceDisconnectWithoutContext("TxErrHeader",
                                             m_neighbors.GetTxErrorCallback());
        }
      }

      // Close socket
      Ptr<Socket> socket = FindSocketWithInterfaceAddress(m_ipv4->GetAddress(interface, 0));
      NS_ASSERT(socket);
      socket->Close();
      m_socketAddresses.erase(socket);
      if (m_socketAddresses.empty())
      {
        NS_LOG_LOGIC("No gpsr interfaces");
        m_neighbors.Clear();
        m_locationService->Clear();
        return;
      }
    }

    /*查找与指定IPv4接口地址关联的Socket，并返回一个指向该Socket的Ptr对象*/
    Ptr<Socket>
    RoutingProtocol::FindSocketWithInterfaceAddress(Ipv4InterfaceAddress addr) const
    {
      NS_LOG_FUNCTION(this << addr);
      for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
               m_socketAddresses.begin();
           j != m_socketAddresses.end(); ++j)
      {
        Ptr<Socket> socket = j->first;
        Ipv4InterfaceAddress iface = j->second;
        if (iface == addr)
        {
          return socket;
        }
      }
      Ptr<Socket> socket;
      return socket;
    }

    /*在IPv4接口添加新的地址时通知路由协议，并为该地址创建一个新的Socket以进行通信。--->hello数据包通信*/
    void RoutingProtocol::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
    {
      NS_LOG_FUNCTION(this << " interface " << interface << " address " << address);
      Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
      if (!l3->IsUp(interface))
      {
        return;
      }
      if (l3->GetNAddresses((interface) == 1))
      {
        Ipv4InterfaceAddress iface = l3->GetAddress(interface, 0);
        Ptr<Socket> socket = FindSocketWithInterfaceAddress(iface);
        if (!socket)
        {
          if (iface.GetLocal() == Ipv4Address("127.0.0.1"))
          {
            return;
          }
          // Create a socket to listen only on this interface
          Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(),
                                                    UdpSocketFactory::GetTypeId());
          NS_ASSERT(socket != 0);
          socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvGPSR, this));
          socket->BindToNetDevice(l3->GetNetDevice(interface));
          // Bind to any IP address so that broadcasts can be received
          socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), GPSR_PORT));
          socket->SetAllowBroadcast(true);
          m_socketAddresses.insert(std::make_pair(socket, iface));

          Ptr<NetDevice> dev = m_ipv4->GetNetDevice(m_ipv4->GetInterfaceForAddress(iface.GetLocal()));
        }
      }
      else
      {
        NS_LOG_LOGIC("GPSR does not work with more then one address per each interface. Ignore added address");
      }
    }
    /*当网络接口的IPv4地址被删除时，调用此方法。*/
    void
    RoutingProtocol::NotifyRemoveAddress(uint32_t i, Ipv4InterfaceAddress address)
    {
      NS_LOG_FUNCTION(this << i << address);
      Ptr<Socket> socket = FindSocketWithInterfaceAddress(address);
      if (socket)
      {

        m_socketAddresses.erase(socket);
        Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
        if (l3->GetNAddresses(i)) // 接口还有地址，创建一个新的socket来监听
        {
          Ipv4InterfaceAddress iface = l3->GetAddress(i, 0);
          // Create a socket to listen only on this interface 创建套接字以仅侦听此接口
          Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(),
                                                    UdpSocketFactory::GetTypeId());
          NS_ASSERT(socket != 0);
          socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvGPSR, this));
          // Bind to any IP address so that broadcasts can be received绑定到任何 IP 地址，以便可以接收广播
          socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), GPSR_PORT));
          socket->SetAllowBroadcast(true);
          m_socketAddresses.insert(std::make_pair(socket, iface));

          // Add local broadcast record to the routing table
          Ptr<NetDevice> dev = m_ipv4->GetNetDevice(m_ipv4->GetInterfaceForAddress(iface.GetLocal()));
        }
        if (m_socketAddresses.empty())
        {
          NS_LOG_LOGIC("No gpsr interfaces");
          m_neighbors.Clear();
          m_locationService->Clear();
          return;
        }
      }
      else
      {
        NS_LOG_LOGIC("Remove address not participating in GPSR operation");
      }
    }
    /*设置与此路由协议关联的Ipv4对象。通常在节点安装路由协议时调用。*/
    void
    RoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
    {
      NS_ASSERT(ipv4 != 0);
      NS_ASSERT(m_ipv4 == 0);

      m_ipv4 = ipv4; // 当前节点ip

      HelloIntervalTimer.SetFunction(&RoutingProtocol::HelloTimerExpire, this);
      double min = 0.0;
      double max = GPSR_MAXJITTER;
      Ptr<UniformRandomVariable> prueba = CreateObject<UniformRandomVariable>();
      prueba->SetAttribute("Min", DoubleValue(min));
      prueba->SetAttribute("Max", DoubleValue(max));

      HelloIntervalTimer.Schedule(Seconds(prueba->GetValue(min, max)));

      // Schedule only when it has packets on queue
      CheckQueueTimer.SetFunction(&RoutingProtocol::CheckQueue, this);

      Simulator::ScheduleNow(&RoutingProtocol::Start, this);
    }
    /* hello消息发送时间间隔设置 */
    void
    RoutingProtocol::HelloTimerExpire()
    {
      SendHello();
      HelloIntervalTimer.Cancel();
      double min = -1 * GPSR_MAXJITTER;
      double max = GPSR_MAXJITTER;
      Ptr<UniformRandomVariable> p_Jitter = CreateObject<UniformRandomVariable>();
      p_Jitter->SetAttribute("Min", DoubleValue(min));
      p_Jitter->SetAttribute("Max", DoubleValue(max));

      HelloIntervalTimer.Schedule(HelloInterval + Seconds(p_Jitter->GetValue(min, max)));
    }

    void
    RoutingProtocol::SendHello() // 广播hello信息，Rsv接受到GPSR
    {
      NS_LOG_FUNCTION(this);
      double positionX;
      double positionY;
      double positionZ;
      std::map<Ipv4Address, float> maxDqvalue;
      maxDqvalue = m_neighbors.GetMaxDQvalue();
      // for (auto i = maxDqvalue.begin(); i != maxDqvalue.end(); i++)
      // {
      //   NS_LOG_UNCOND("send maxDqvalue" << i->first << i->second);
      // }

      Ptr<MobilityModel> MM = m_ipv4->GetObject<MobilityModel>();

      positionX = MM->GetPosition().x;
      positionY = MM->GetPosition().y;
      positionZ = MM->GetPosition().z;

      for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin(); j != m_socketAddresses.end(); ++j)
      {
        Ptr<Socket> socket = j->first;
        Ipv4InterfaceAddress iface = j->second;
        HelloHeader helloHeader(((uint32_t)positionX), ((uint32_t)positionY), ((uint32_t)positionZ), maxDqvalue, currentdst);

        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(helloHeader);
        TypeHeader tHeader(GPSRTYPE_HELLO);
        packet->AddHeader(tHeader);

        Ipv4Address destination;

        if (iface.GetMask() == Ipv4Mask::GetOnes())
        {
          destination = Ipv4Address("255.255.255.255");
        }
        else
        {
          destination = iface.GetBroadcast();
        }
        uint32_t RxAvailable = socket->GetRxAvailable();
        auto tag1 = customTag(RxAvailable, senderNodesNum, receivedNodesNum, delayTag);
        packet->AddPacketTag(tag1);
        socket->SendTo(packet, 0, InetSocketAddress(destination, GPSR_PORT));
      }
    }
    /* 检测是否是本地接口地址 */
    bool
    RoutingProtocol::IsMyOwnAddress(Ipv4Address src)
    {
      NS_LOG_FUNCTION(this << src);
      for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j =
               m_socketAddresses.begin();
           j != m_socketAddresses.end(); ++j)
      {
        Ipv4InterfaceAddress iface = j->second;
        if (src == iface.GetLocal())
        {
          return true;
        }
      }
      return false;
    }

    /* 协议开始，设置有关参数 */
    void
    RoutingProtocol::Start()
    {
      NS_LOG_FUNCTION(this);
      m_queuedAddresses.clear();

      // FIXME ajustar timer, meter valor parametrizavel
      Time tableTime("2s");
      m_selfIpv4Address = m_ipv4->GetAddress(1, 0).GetLocal();
      switch (LocationServiceName)
      {
      case GPSR_LS_GOD:
        NS_LOG_DEBUG("GodLS in use");
        m_locationService = CreateObject<GodLocationService>();
        break;
      case GPSR_LS_RLS:
        NS_LOG_UNCOND("RLS not yet implemented");
        break;
      }
    }
    Ptr<Ipv4Route>
    RoutingProtocol::LoopbackRoute(const Ipv4Header &hdr, Ptr<NetDevice> oif)
    {
      NS_LOG_FUNCTION(this << hdr);
      m_lo = m_ipv4->GetNetDevice(0);
      NS_ASSERT(m_lo != 0);
      Ptr<Ipv4Route> rt = Create<Ipv4Route>();
      rt->SetDestination(hdr.GetDestination());

      std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin();
      if (oif)
      {
        // Iterate to find an address on the oif device
        for (j = m_socketAddresses.begin(); j != m_socketAddresses.end(); ++j)
        {
          Ipv4Address addr = j->second.GetLocal();
          int32_t interface = m_ipv4->GetInterfaceForAddress(addr);
          if (oif == m_ipv4->GetNetDevice(static_cast<uint32_t>(interface)))
          {
            rt->SetSource(addr);
            break;
          }
        }
      }
      else
      {
        rt->SetSource(j->second.GetLocal());
      }
      NS_ASSERT_MSG(rt->GetSource() != Ipv4Address(), "Valid GPSR source address not found");
      rt->SetGateway(Ipv4Address("127.0.0.1"));
      rt->SetOutputDevice(m_lo);

      return rt;
    }

    int
    RoutingProtocol::GetProtocolNumber(void) const
    {
      return GPSR_PORT;
    }
    /* 在gpsrhelper那里调用 */
    void
    RoutingProtocol::AddHeaders(Ptr<Packet> p, Ipv4Address source, Ipv4Address destination, uint8_t protocol, Ptr<Ipv4Route> route)
    {

      NS_LOG_FUNCTION(this << " source " << source << " destination " << destination);

      Vector myPos;
      Ptr<MobilityModel> MM = m_ipv4->GetObject<MobilityModel>();
      myPos.x = MM->GetPosition().x;
      myPos.y = MM->GetPosition().y;
      myPos.z = MM->GetPosition().z;

      Ipv4Address nextHop;

      if (m_neighbors.isNeighbour(destination))
      {
        nextHop = destination;
      }
      else if (destination == Ipv4Address("10.0.255.255"))
      {
        // nextHop = m_neighbors.BestNeighbor(m_locationService->GetPosition(destination), myPos);
      }
      else
      {
        nextHop = m_neighbors.QLGR(Ipv4Address::GetZero(), myPos, m_locationService->GetPosition(destination), destination);
      }
      // nextHop = m_neighbors.BestNeighbor(m_locationService->GetPosition(destination), myPos);
      // nextHop = m_neighbors.QLGR(myPos, m_locationService->GetPosition(destination), destination);

      uint16_t positionX = 0;
      uint16_t positionY = 0;
      uint16_t positionZ = 0;
      uint32_t hdrTime = 0;

      if (destination != m_ipv4->GetAddress(1, 0).GetBroadcast()) // 目的地址不是广播地址，广播地址是指所有节点可访问
      {
        positionX = m_locationService->GetPosition(destination).x;
        positionY = m_locationService->GetPosition(destination).y;
        positionZ = m_locationService->GetPosition(destination).z;
        hdrTime = (uint32_t)m_locationService->GetEntryUpdateTime(destination).GetSeconds(); // 目的位置更新时间
      }
      PositionHeader posHeader(positionX, positionY, positionZ, m_selfIpv4Address, hdrTime, (uint32_t)0, (uint32_t)0, (uint32_t)0, (uint8_t)0, myPos.x, myPos.y, myPos.z);
      p->AddHeader(posHeader); // FIXME不同构造函数对象可以取出数据吗
      TypeHeader tHeader(GPSRTYPE_POS);
      p->AddHeader(tHeader);

      m_downTarget(p, source, destination, protocol, route); // 通过 IPv4 发送数据包的回调
    }

    bool
    RoutingProtocol::Forwarding(Ptr<const Packet> packet, const Ipv4Header &header,
                                UnicastForwardCallback ucb, ErrorCallback ecb)
    {
      Ptr<Packet> p = packet->Copy();
      NS_LOG_FUNCTION(this);
      Ipv4Address dst = header.GetDestination();
      Ipv4Address origin = header.GetSource();
      m_neighbors.Purge();

      uint32_t updated = 0;
      Vector Position;                             // 目的节点位置
      Vector RecPosition;                          // 周边转发的位置
      uint8_t inRec = 0;                           // 1表示正处于周边转发情况下
      Ipv4Address prehop = Ipv4Address::GetZero(); // 前一跳节点
      TypeHeader tHeader(GPSRTYPE_POS);
      PositionHeader hdr;
      p->RemoveHeader(tHeader); /*反序列化并从内部缓冲区中删除标头。*/

      if (!tHeader.IsValid())
      {
        NS_LOG_DEBUG("GPSR message " << p->GetUid() << " with unknown type received: " << tHeader.Get() << ". Drop");
        return false; // drop
      }
      if (tHeader.Get() == GPSRTYPE_POS)
      {

        p->RemoveHeader(hdr);
        Position.x = hdr.GetDstPosx();
        Position.y = hdr.GetDstPosy();
        Position.z = hdr.GetDstPosz();
        prehop = hdr.GetPrehop();
        updated = hdr.GetUpdated();
        RecPosition.x = hdr.GetRecPosx();
        RecPosition.y = hdr.GetRecPosy();
        RecPosition.z = hdr.GetRecPosz();
        inRec = hdr.GetInRec();
      }
      /* step3:获取回传标签 */
      backPacketTag backtag02;
      if (p->PeekPacketTag(backtag02)) /* 目前只有forwarding才会回传 */
      {
        // 能够接收到数据的一定是回传节点,当前节点移除backtag,作出惩罚
        p->RemovePacketTag(backtag02);
        m_neighbors.Update_LR_QTable(prehop, dst, -1);
      }
      Vector myPos;
      Ptr<MobilityModel> MM = m_ipv4->GetObject<MobilityModel>(); // TODOm_ipv4是当前节点的IP地址
      myPos.x = MM->GetPosition().x;
      myPos.y = MM->GetPosition().y;
      myPos.z = MM->GetPosition().z;

      if (inRec == 1 && CalculateDistance(myPos, Position) < CalculateDistance(RecPosition, Position))
      {
        inRec = 0;
        hdr.SetInRec(0);
        NS_LOG_LOGIC("No longer in Recovery to " << dst << " in " << myPos);
      }

      if (inRec)
      {
        p->AddHeader(hdr);
        p->AddHeader(tHeader); // put headers back so that the RecoveryMode is compatible with Forwarding and SendFromQueue
        // 将标头放回原处，以便恢复模式与转发和发送队列兼容
        RecoveryMode(dst, p, ucb, header);
        return true;
      }

      uint32_t myUpdated = (uint32_t)m_locationService->GetEntryUpdateTime(dst).GetSeconds();
      if (myUpdated > updated) // check if node has an update to the position of destination更新目标节点位置
      {
        Position.x = m_locationService->GetPosition(dst).x;
        Position.y = m_locationService->GetPosition(dst).y;
        Position.z = m_locationService->GetPosition(dst).z;
        updated = myUpdated;
      }
      Ipv4Address nextHop;
      nextHop = m_neighbors.QLGR(prehop, myPos, Position, header.GetDestination());
      if (nextHop != Ipv4Address::GetZero())
      {
        backPacketTag mybackTag;
        /* step1:添加回传标签 */
        if (nextHop == prehop && (!p->PeekPacketTag(mybackTag)))
          p->AddPacketTag(mybackTag);

        /* step2:转发过程中把当前ip赋值给prehop */
        PositionHeader posHeader(Position.x, Position.y, Position.z, m_selfIpv4Address, updated, (uint32_t)0, (uint32_t)0, (uint8_t)0, myPos.x, myPos.y, myPos.z);
        p->AddHeader(posHeader);
        p->AddHeader(tHeader);

        Ptr<NetDevice> oif = m_ipv4->GetObject<NetDevice>();
        Ptr<Ipv4Route> route = Create<Ipv4Route>();
        route->SetDestination(dst);
        route->SetSource(header.GetSource());
        route->SetGateway(nextHop);
        // NS_LOG_UNCOND("forward:当前节点:"<<m_selfIpv4Address<<"前一跳节点:" << prehop<< "下一跳节点:"<<nextHop);
        // FIXME: Does not work for multiple interfaces
        route->SetOutputDevice(m_ipv4->GetNetDevice(1));
        route->SetDestination(header.GetDestination());
        NS_ASSERT(route != 0);
        NS_LOG_DEBUG("Exist route to " << route->GetDestination() << " from interface " << route->GetOutputDevice());

        NS_LOG_LOGIC(route->GetOutputDevice() << " forwarding to " << dst << " from " << origin << " through " << route->GetGateway() << " packet " << p->GetUid());

        ucb(route, p, header); // 要转发的单播数据包的回调。
        return true;
      }
      //    }
      hdr.SetInRec(1);
      // FIXME:这里是周边转发的坐标，周边转发无法用三维坐标，这里后面还要改
      hdr.SetRecPosx(myPos.x);
      hdr.SetRecPosy(myPos.y);
      hdr.SetRecPosz(myPos.z);
      hdr.SetLastPosx(Position.x); // when entering Recovery, the first edge is the Dst
      hdr.SetLastPosy(Position.y); // 周边转发时的目的节点位置
      hdr.SetLastPosz(Position.z);

      p->AddHeader(hdr);
      p->AddHeader(tHeader);
      NS_LOG_LOGIC("Entering recovery-mode to " << dst << " in " << m_ipv4->GetAddress(1, 0).GetLocal());

      RecoveryMode(dst, p, ucb, header);
      return true;
    }

    void
    RoutingProtocol::SetDownTarget(IpL4Protocol::DownTargetCallback callback)
    {
      m_downTarget = callback;
    }

    IpL4Protocol::DownTargetCallback
    RoutingProtocol::GetDownTarget(void) const
    {
      return m_downTarget;
    }

    /*为本地产生的数据包选择下一跳。它返回一个路由实体，包含下一跳的地址和出口网卡。如果没有找到合适的路由，返回空路由。*/
    Ptr<Ipv4Route>
    RoutingProtocol::RouteOutput(Ptr<Packet> p, const Ipv4Header &header,
                                 Ptr<NetDevice> oif, Socket::SocketErrno &sockerr)
    {
      NS_LOG_FUNCTION(this << header << (oif ? oif->GetIfIndex() : 0));

      if (!p)
      {
        return LoopbackRoute(header, oif); // later
      }
      if (m_socketAddresses.empty())
      {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        NS_LOG_LOGIC("No gpsr interfaces");
        Ptr<Ipv4Route> route;
        return route;
      }
      sockerr = Socket::ERROR_NOTERROR;
      Ptr<Ipv4Route> route = Create<Ipv4Route>();
      Ipv4Address dst = header.GetDestination();
      Vector dstPos = Vector(1, 0, 0);

      if (!(dst == m_ipv4->GetAddress(1, 0).GetBroadcast()))
      {
        dstPos = m_locationService->GetPosition(dst);
      }

      if (CalculateDistance(dstPos, m_locationService->GetInvalidPosition()) == 0 && m_locationService->IsInSearch(dst))
      {
        DeferredRouteOutputTag tag;
        if (!p->PeekPacketTag(tag))
        {
          p->AddPacketTag(tag);
        }
        return LoopbackRoute(header, oif); // 从环回接收并传递到RouteInput
      }                                    // 肯定不会进入

      Vector myPos;
      Ptr<MobilityModel> MM = m_ipv4->GetObject<MobilityModel>();
      myPos.x = MM->GetPosition().x;
      myPos.y = MM->GetPosition().y;
      myPos.z = MM->GetPosition().z;

      Ipv4Address nextHop; // 102.102.102.102

      // m_neighbors.PrintTable();

      if (m_neighbors.isNeighbour(dst))
      {
        nextHop = dst;
        m_neighbors.Update_GR_QTable(nextHop, dst, 10);
      }
      else
      { /* 这里不可能存在上一跳 */
        nextHop = m_neighbors.QLGR(Ipv4Address::GetZero(), myPos, dstPos, dst);
      }

      if (nextHop != Ipv4Address::GetZero())
      {
        NS_LOG_DEBUG("Destination: " << dst);

        route->SetDestination(dst);
        if (header.GetSource() == Ipv4Address("102.102.102.102")) //
        {
          route->SetSource(m_ipv4->GetAddress(1, 0).GetLocal());
        }
        else
        {
          route->SetSource(header.GetSource());
        }
        route->SetGateway(nextHop);
        route->SetOutputDevice(m_ipv4->GetNetDevice(m_ipv4->GetInterfaceForAddress(route->GetSource())));
        route->SetDestination(header.GetDestination());
        NS_ASSERT(route != 0);
        NS_LOG_DEBUG("Exist route to " << route->GetDestination() << " from interface " << route->GetSource());
        if (oif != 0 && route->GetOutputDevice() != oif)
        {
          NS_LOG_DEBUG("Output device doesn't match. Dropped.");
          sockerr = Socket::ERROR_NOROUTETOHOST;
          return Ptr<Ipv4Route>();
        }
        // FIXME 数据包存在上一跳：打上tag，接收到数据包的函数作惩罚
        // 1、回传给上一跳，选择算法根据tag惩罚后,进行下一轮路由选择
        // 2、禁止回传，加入延时队列，30s
        senderNodesNum += 1;
        delayTag = 0;

        return route;
      }
      else
      {
        // m_neighbors.Update_GR_QTable(,dst,-1);
        DeferredRouteOutputTag tag; // 添加了这个tag恢复模式肯定会被调用，为什么会调用RouteInput？？？
        if (!p->PeekPacketTag(tag))
        {
          p->AddPacketTag(tag);
        }
        delayTag = 1;
        return LoopbackRoute(header, oif); // in RouteInput the recovery-mode is called在RouteInput中恢复模式被调用
      }
    }

  }
}

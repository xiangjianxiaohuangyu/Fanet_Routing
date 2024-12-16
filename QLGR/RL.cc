#include "RL.h"
#define GPSR_LS_GOD 0
#define GPSR_LS_RLS 1
#define GPSR_MAXJITTER (HelloInterval.GetSeconds() / 2)
namespace ns3
{
    NS_LOG_COMPONENT_DEFINE("subRllog");
    namespace gpsr
    {
        subRl::subRl()
        {
            NS_LOG_FUNCTION(this);
        };
        subRl::~subRl()
        {
            NS_LOG_FUNCTION(this);
        }
        NS_OBJECT_ENSURE_REGISTERED(subRl);
        TypeId subRl::GetTypeId()
        {
            static TypeId tid = TypeId("ns3::gpsr::subRl")
                                    .AddConstructor<subRl>()
                                    .SetParent<RoutingProtocol>()
                                    .AddAttribute("mygymenv", "add my gymenv", PointerValue(),
                                                  MakePointerAccessor(&subRl::m_mygymenv),
                                                  MakePointerChecker<MyGymEnv>())
                                    .AddTraceSource("rPacket", "received packet", MakeTraceSourceAccessor(&subRl::m_r),
                                                    "ns3::gpsr::subRl::PacketReciveCallback")
                                    .AddTraceSource("sPacket", "send packet", MakeTraceSourceAccessor(&subRl::m_s),
                                                    "ns3::gpsr::subRl::PacketSendCallback")
                                    .AddAttribute("myQ", "help text", DoubleValue(0.0), MakeDoubleAccessor(&subRl::Q_value),

                                                  MakeDoubleChecker<double>());

            return tid;
        }
        void subRl::DoDispose()
        {
            RoutingProtocol::DoDispose();
        }

        /* -------------------------------------------------------------------------- */
        /*                                    重写方法                                   */
        /* -------------------------------------------------------------------------- */
        struct customTag : public Tag
        {
            // 从wifimac和socket中获取缓存队列
            uint32_t m_buff_queue;

            customTag(uint32_t aa) : Tag(),
                                     m_buff_queue(0)
            {
                m_buff_queue = aa;
            }
            customTag() : Tag(),
                          m_buff_queue(0)
            {
            }
            static TypeId GetTypeId()
            {
                static TypeId tid = TypeId("ns3::gpsr::customTag").SetParent<Tag>();
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
                i.WriteU32(m_buff_queue);
            }

            void Deserialize(TagBuffer i)
            {
                m_buff_queue = i.ReadU32();
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
                static TypeId tid = TypeId("ns3::gpsr::DeferredRouteOutputTag").SetParent<Tag>();
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
                // os << "DeferredRouteOutputTag: m_isCallFromL3 = " << m_isCallFromL3<<"\n";
            }
        };
        // FIXME等下在这里获取当前节点
        /*  为从外部接收到的数据包选择下一跳。如果数据包的目的地址是本节点，则应将数据包传递给上层协议。否则，根据路由表选择下一跳。*/
        bool subRl::RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
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

            DeferredRouteOutputTag tag; // FIXME since I have to check if it's in origin for it to work it means I'm not taking some tag out...

            if (p->PeekPacketTag(tag) && IsMyOwnAddress(origin))
            {
                m_mygymenv->DeferredRouteOutput = tag.m_isCallFromL3;//我在这里把延迟容忍队列传过去了！！！
                m_mygymenv->mydst = dst;
                Ptr<Packet> packet = p->Copy(); // FIXME ja estou a abusar de tirar tags
                packet->RemovePacketTag(tag);
                DeferredRouteOutput(packet, header, ucb, ecb);
                return true;
            }

            if (m_ipv4->IsDestinationAddress(dst, iif)) // 确定接收到的数据包对应的地址和接口是否可以接受本地传送,
            //hello信息设置的除发送节点外，所有节点都是目的节点，所以这个判断下，hello和下一跳是目的节点的pos类型数据包会经过
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
                    NS_LOG_LOGIC("Unicast local delivery to " << dst);
                }
                else
                {
                    //          NS_LOG_LOGIC ("Broadcast local delivery to " << dst);
                }

                lcb(packet, header, iif);
                return true;
            }

            return Forwarding(p, header, ucb, ecb); // 转发
        }

        /* 协议开始，设置有关参数 */
        void
        subRl::Start()
        {

            NS_LOG_FUNCTION(this);
            m_queuedAddresses.clear();

            // FIXME ajustar timer, meter valor parametrizavel
            Time tableTime("2s");

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
        void subRl::RecvGPSR(Ptr<Socket> socket) /*接受到hello数据包然后更新邻居表*/
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
            m_r(m_ipv4->GetAddress(1, 0).GetLocal()); // 数据包接受追踪事件

            HelloHeader hdr;
            packet->RemoveHeader(hdr);
            Vector Position;
            Position.x = hdr.GetOriginPosx();
            Position.y = hdr.GetOriginPosy();
            Position.z = hdr.GetOriginPosz();
            InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom(sourceAddress);
            Ipv4Address sender = inetSourceAddr.GetIpv4();
            Ipv4Address receiver = m_socketAddresses[socket].GetLocal();
            // NS_LOG_UNCOND("N:" << m_socketAddresses.size());
            NS_ASSERT(m_mygymenv != 0); // 强化学习对象不能为空
            // NS_LOG_UNCOND( "delay queue:"<<m_queue.GetSize());
            m_mygymenv->neighborsNumb = tag1.m_buff_queue; // 邻居节点的邻居节点数
            
            m_mygymenv->myNeighbors = m_neighbors;         // 邻居表传过去
            m_mygymenv->m_ip = m_ipv4;                     // ipv4传过去

            m_mygymenv->currentid = receiver.Get(); // 当前节点
            m_mygymenv->neigboorid = sender.Get();  // 邻居节点

            m_mygymenv->Notify(); // 强化学习更新状态
            Q_value = m_mygymenv->updateQ();//将Q值从强化学习环境中传入
            if (Q_value == -1)
            {
                Q_value = 0;
            }
            
            //NS_LOG_UNCOND("1Q:"<<Q_value);
            UpdateRouteToNeighbor(sender, Position, receiver, Q_value); // sender Position表示发出hello信息的节点ip和位置
        }
        // 更新邻居表也能更新缓存队列参数sender邻居、Pos邻居位置、当前节点receiver、缓存队列queue
        void subRl::UpdateRouteToNeighbor(Ipv4Address sender, Vector Pos, Ipv4Address receiver, float q_value)
        {
            NS_LOG_FUNCTION(this << sender << receiver << Pos);
           
            m_neighbors.AddEntry(sender, Pos, receiver, q_value);
        }

        /*查找与指定IPv4接口地址关联的Socket，并返回一个指向该Socket的Ptr对象*/
        Ptr<Socket>
        subRl::FindSocketWithInterfaceAddress(Ipv4InterfaceAddress addr) const
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
        void subRl::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
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
                    socket->SetRecvCallback(MakeCallback(&subRl::RecvGPSR, this));
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
        void subRl::NotifyRemoveAddress(uint32_t i, Ipv4InterfaceAddress address)
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
                    socket->SetRecvCallback(MakeCallback(&subRl::RecvGPSR, this));

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
        void
        subRl::SetIpv4(Ptr<Ipv4> ipv4)
        {
            // NS_LOG_UNCOND(m_mygymenv);//保证传过来的强化学习对象是同一个
            NS_ASSERT(ipv4 != 0);
            NS_ASSERT(m_ipv4 == 0);

            m_ipv4 = ipv4; // 当前节点ip

            HelloIntervalTimer.SetFunction(&subRl::HelloTimerExpire, this);
            double min = 0.0;
            double max = GPSR_MAXJITTER;
            Ptr<UniformRandomVariable> prueba = CreateObject<UniformRandomVariable>();
            prueba->SetAttribute("Min", DoubleValue(min));
            prueba->SetAttribute("Max", DoubleValue(max));

            HelloIntervalTimer.Schedule(Seconds(prueba->GetValue(min, max)));

            // Schedule only when it has packets on queue
            CheckQueueTimer.SetFunction(&subRl::CheckQueue, this);

            Simulator::ScheduleNow(&subRl::Start, this);
        }
        /* hello消息发送时间间隔设置 */
        void subRl::HelloTimerExpire()
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

        void subRl::SendHello() // 广播hello信息，Rsv接受到hello消息
        {
            NS_LOG_FUNCTION(this);
            double positionX;
            double positionY;

            Ptr<MobilityModel> MM = m_ipv4->GetObject<MobilityModel>();

            positionX = MM->GetPosition().x;
            positionY = MM->GetPosition().y;

            for (std::map<Ptr<Socket>, Ipv4InterfaceAddress>::const_iterator j = m_socketAddresses.begin(); j != m_socketAddresses.end(); ++j)
            {
                Ptr<Socket> socket = j->first;
                Ipv4InterfaceAddress iface = j->second;
                HelloHeader helloHeader(((uint64_t)positionX), ((uint64_t)positionY));

                Ptr<Packet> packet = Create<Packet>();
                packet->AddHeader(helloHeader);
                TypeHeader tHeader(GPSRTYPE_HELLO);
                packet->AddHeader(tHeader);
                // Send to all-hosts broadcast if on /32 addr, subnet-directed otherwise
                Ipv4Address destination;

                if (iface.GetMask() == Ipv4Mask::GetOnes())
                {
                    destination = Ipv4Address("255.255.255.255");
                }
                else
                {
                    destination = iface.GetBroadcast();
                }
                PointerValue ptr;
                m_mac->GetAttribute("Txop", ptr);
                Ptr<Txop> txop = ptr.Get<Txop>();
                Ptr<WifiMacQueue> queue = txop->GetWifiMacQueue();
                // NS_LOG_UNCOND("the queue :" << queue->GetNBytes()); // 获取mac的数据列表
                // NS_LOG_UNCOND("tx :" << socket->GetTxAvailable());  // 获取发送缓存大小
                // NS_LOG_UNCOND("rx :" << socket->GetRxAvailable());
                // NS_LOG_UNCOND("size:" << m_neighbors.getNeiboorSize()); // 获取邻居节点数量
                auto tag1 = customTag(m_neighbors.getNeiboorSize());    // 获取邻居节点数量
                //NS_LOG_UNCOND("Current Ip:" << m_ipv4->GetAddress(1, 0).GetLocal());

                packet->AddPacketTag(tag1);
                socket->SendTo(packet, 0, InetSocketAddress(destination, GPSR_PORT)); // 之前创建了监听该端口GPSR_PORT的socket，
            }                                                                         // 现在绑定该端口进行发送
        }

        bool subRl::Forwarding(Ptr<const Packet> packet, const Ipv4Header &header,
                               UnicastForwardCallback ucb, ErrorCallback ecb)
        {
            Ptr<Packet> p = packet->Copy();
            NS_LOG_FUNCTION(this);
            Ipv4Address dst = header.GetDestination();
            Ipv4Address origin = header.GetSource();

            m_neighbors.Purge();

            uint32_t updated = 0;
            Vector Position;    // 目的节点位置
            Vector RecPosition; // 周边转发的位置
            uint8_t inRec = 0;  // 1表示正处于周边转发情况下

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
                updated = hdr.GetUpdated();
                RecPosition.x = hdr.GetRecPosx();
                RecPosition.y = hdr.GetRecPosy();
                RecPosition.z = hdr.GetRecPosz();
                inRec = hdr.GetInRec();
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

            /*  if(m_neighbors.isNeighbour (dst))
                {
                  nextHop = dst;
                }
              else
                {
            */
            // TODO我要做的就是更改下一跳节点
            nextHop = m_neighbors.BestNeighbor(Position, myPos);

            if (nextHop != Ipv4Address::GetZero())
            {
                PositionHeader posHeader(Position.x, Position.y,Position.z, updated, (uint64_t)0, (uint64_t)0, (uint8_t)0, myPos.x, myPos.y,myPos.z);
                p->AddHeader(posHeader);
                p->AddHeader(tHeader);

                Ptr<NetDevice> oif = m_ipv4->GetObject<NetDevice>();
                Ptr<Ipv4Route> route = Create<Ipv4Route>();
                route->SetDestination(dst);
                route->SetSource(header.GetSource());
                route->SetGateway(nextHop);

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
        /*为本地产生的数据包选择下一跳。它返回一个路由实体，包含下一跳的地址和出口网卡。如果没有找到合适的路由，返回空路由。*/
        Ptr<Ipv4Route>
        subRl::RouteOutput(Ptr<Packet> p, const Ipv4Header &header,
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
            }

            Vector myPos;
            Ptr<MobilityModel> MM = m_ipv4->GetObject<MobilityModel>();
            myPos.x = MM->GetPosition().x;
            myPos.y = MM->GetPosition().y;
            myPos.z = MM->GetPosition().z;

            Ipv4Address nextHop;

            //m_neighbors.PrintTable();

            if (m_neighbors.isNeighbour(dst))
            {
                nextHop = dst;
            }
            else
            {
                nextHop = m_neighbors.BestNeighbor(dstPos, myPos);
            }

            if (nextHop != Ipv4Address::GetZero())
            {
                NS_LOG_DEBUG("Destination: " << dst);

                route->SetDestination(dst);
                if (header.GetSource() == Ipv4Address("102.102.102.102"))  //这个ip地址有什么特别含义？
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
                m_s(m_ipv4->GetAddress(1, 0).GetLocal()); // 发送数据包追踪
            
                return route;
            }
            else
            {
                DeferredRouteOutputTag tag; //添加了这个tag恢复模式肯定会被调用，为什么会调用RouteInput？？？
                if (!p->PeekPacketTag(tag))
                {
                    p->AddPacketTag(tag);
                }
                return LoopbackRoute(header, oif); //in RouteInput the recovery-mode is called在RouteInput中恢复模式被调用
            }
        }
        /*当网络接口状态变为“UP”时，调用此方法。自定义路由协议可以在此方法中执行与接口启用相关的操作。*/
        void
        subRl::NotifyInterfaceUp(uint32_t interface)
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
            socket->SetRecvCallback(MakeCallback(&subRl::RecvGPSR, this));

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
            m_mac = mac;

            mac->TraceConnectWithoutContext("TxErrHeader", m_neighbors.GetTxErrorCallback());
        }

    }

}
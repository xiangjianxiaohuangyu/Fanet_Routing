/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef QGPSR_H
#define QGPSR_H

#include "qgpsr_ptable.h"
#include "ns3/node.h"
#include "qgpsr_packet.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ip-l4-protocol.h"
#include "ns3/mobility-model.h"
#include "qgpsr_queue.h"
#include "ns3/core-module.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4-route.h"
#include "ns3/location-service.h"
#include "ns3/god.h"
#include <map>
#include <complex>

namespace ns3
{


    namespace qgpsr
    {

        class RoutingProtocol : public Ipv4RoutingProtocol
        {
        public:
            static TypeId GetTypeId(void);
            static const uint32_t GPSR_PORT;

            /// c-tor
            RoutingProtocol();
            virtual ~RoutingProtocol();
            virtual void DoDispose();

            /*为本地产生的数据包选择下一跳。它返回一个路由实体，包含下一跳的地址和出口网卡。如果没有找到合适的路由，返回空路由。*/
            Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);
            /*  为从外部接收到的数据包选择下一跳。如果数据包的目的地址是本节点，则应将数据包传递给上层协议。否则，根据路由表选择下一跳。*/
            bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                            UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                            LocalDeliverCallback lcb, ErrorCallback ecb);
            /*当网络接口状态变为“UP”时，调用此方法。自定义路由协议可以在此方法中执行与接口启用相关的操作。*/
            virtual void NotifyInterfaceUp(uint32_t interface);
            int GetProtocolNumber(void) const;
            virtual void AddHeaders(Ptr<Packet> p, Ipv4Address source, Ipv4Address destination, uint8_t protocol, Ptr<Ipv4Route> route);
            virtual void NotifyInterfaceDown(uint32_t interface);
            virtual void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address);
            virtual void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address);
            virtual void SetIpv4(Ptr<Ipv4> ipv4); /*静态函数，返回一个TypeId对象。该对象用于在ns-3的TypeId系统中注册路由协议。*/
            virtual void RecvGPSR(Ptr<Socket> socket);
            virtual void UpdateRouteToNeighbor(Ipv4Address sender, Vector Pos, Ipv4Address receiver);
            /* 通过接收到Hello消息更新Q表 */
            void Update_Qtable(HelloHeader &hdr, Ipv4Address sender, float LR);
            virtual void SendHello();
            virtual bool IsMyOwnAddress(Ipv4Address src);

            Ptr<Ipv4> m_ipv4;
            /// Raw socket per each IP interface, map socket -> iface address (IP + mask)每个 IP 接口的原始套接字，映射套接字 -> iface 地址（IP + 掩码）
            std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketAddresses; // 存放的只有当前对象的一组数据
            /// Loopback device used to defer RREQ until packet will be fully formed
            Ptr<NetDevice> m_lo;

            Ptr<LocationService> GetLS();
            void SetLS(Ptr<LocationService> locationService);

            /// Broadcast ID
            uint32_t m_requestId;
            /// Request sequence number 请求序列号
            uint32_t m_seqNo;

            /// Number of RREQs used for RREQ rate control用于 RREQ 速率控制的 RREQ 数
            uint16_t m_rreqCount;
            Time HelloInterval;

            void SetDownTarget(IpL4Protocol::DownTargetCallback callback);
            IpL4Protocol::DownTargetCallback GetDownTarget(void) const;

            virtual void PrintRoutingTable(ns3::Ptr<ns3::OutputStreamWrapper>, Time::Unit unit = Time::S) const

            {
                return;
            }

            /// Start protocol operation
            void Start();
            /// Queue packet and send route request排队数据包并发送路由请求
            void DeferredRouteOutput(Ptr<const Packet> p, const Ipv4Header &header, UnicastForwardCallback ucb, ErrorCallback ecb);
            /// If route exists and valid, forward packet.
            void HelloTimerExpire();

            /// Queue packet and send route request
            Ptr<Ipv4Route> LoopbackRoute(const Ipv4Header &header, Ptr<NetDevice> oif);

            /// If route exists and valid, forward packet.如果路由存在且有效，则转发数据包。
            bool Forwarding(Ptr<const Packet> p, const Ipv4Header &header, UnicastForwardCallback ucb, ErrorCallback ecb);

            /// Find socket with local interface address iface 查找具有本地接口地址 iface 的套接字
            Ptr<Socket> FindSocketWithInterfaceAddress(Ipv4InterfaceAddress iface) const;

            // Check packet from deffered route output queue and send if position is already available
            // 检查来自延迟路由输出队列的数据包，如果位置已经可用，则发送
            // returns true if the IP should be erased from the list (was sent/droped)
            // 如果应从列表中擦除 IP（已发送/删除），则返回 true。
            bool SendPacketFromQueue(Ipv4Address dst);

            // Calls SendPacketFromQueue and re-schedules
            void CheckQueue();

            void RecoveryMode(Ipv4Address dst, Ptr<Packet> p, UnicastForwardCallback ucb, Ipv4Header header);

            uint32_t MaxQueueLen;
            Time MaxQueueTime;
            RequestQueue m_queue;

            Timer HelloIntervalTimer;
            Timer CheckQueueTimer;
            uint8_t LocationServiceName;
            PositionTable m_neighbors;
            bool PerimeterMode;
            std::list<Ipv4Address> m_queuedAddresses;
            Ptr<LocationService> m_locationService;
            IpL4Protocol::DownTargetCallback m_downTarget;
            Vector myPos;
            /* 当前节点ip地址 */
            Ipv4Address m_selfIpv4Address = Ipv4Address::GetZero();
            /* 统计单个节点发送数量 */
            uint32_t senderNodesNum;
            /* 统计单个节点接收数量 */
            uint32_t receivedNodesNum;
            /* 当前节点对应的目的节点 */
            Ipv4Address currentdst = Ipv4Address::GetZero();
            /* 延迟队列的标志 */
           uint8_t delayTag = 0;
           float alpha = 0.6;
        };
    }
}
#endif

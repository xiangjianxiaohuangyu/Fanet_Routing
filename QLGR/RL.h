/*
 * @Author: WuShengPing123 1745017741@qq.com
 * @Date: 2023-06-29 21:41:17
 * @LastEditors: WuShengPing123 1745017741@qq.com
 * @LastEditTime: 2023-11-09 20:41:26
 * @FilePath: /ns-3.29/scratch/QLGR/RL.h
 *
 */
#include "ns3/gpsr-module.h"
#include "ns3/core-module.h"
#include "ns3/opengym-module.h"
#include "mygym.h"
#include "ns3/udp-socket-factory.h"
#include <algorithm>
#include <deque>
#include <vector>

namespace ns3
{
    namespace gpsr
    {

        class subRl : public RoutingProtocol
        {
        public:
            subRl();
            virtual ~subRl();
            static TypeId GetTypeId();
            virtual void DoDispose();
            Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);
            /*  为从外部接收到的数据包选择下一跳。如果数据包的目的地址是本节点，则应将数据包传递给上层协议。否则，根据路由表选择下一跳。*/
            bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev,
                            UnicastForwardCallback ucb, MulticastForwardCallback mcb,
                            LocalDeliverCallback lcb, ErrorCallback ecb);
            void Start();
            void RecvGPSR(Ptr<Socket> socket);
            Ptr<Socket> FindSocketWithInterfaceAddress(Ipv4InterfaceAddress iface) const;
            void NotifyRemoveAddress(uint32_t i, Ipv4InterfaceAddress address);
            void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address);
            void SetIpv4(Ptr<Ipv4> ipv4);
            bool Forwarding(Ptr<const Packet> p, const Ipv4Header &header, UnicastForwardCallback ucb, ErrorCallback ecb);
            void NotifyInterfaceUp(uint32_t interface);
            void HelloTimerExpire();
            void SendHello();
            void UpdateRouteToNeighbor(Ipv4Address sender, Vector Pos, Ipv4Address receiver, float queue);
            Ptr<MyGymEnv> m_mygymenv;
            TracedCallback<Ipv4Address> m_r;
            typedef void (*PacketReciveCallback)(Ipv4Address);
            TracedCallback<Ipv4Address> m_s;
            typedef void (*PacketSendCallback)(Ipv4Address);
            Ptr<WifiMac> m_mac; // 当前gpsr对象的mac对象
            float Q_value;      // Q值
            /* -------------------------移动预测----------------------- */
            // 记录位置历史
            void trackPosition(Vector3D pos);

            // 预测未来位置
            Vector3D forecastPosition();

            // 用目标点预测位置
            Vector3D predictWithTarget(Vector3D current, int dt, Vector3D wp);

            // 用历史位置预测
            Vector3D predictWithHistory(std::deque<Vector3D> history, std::deque<Time> times, int t);

            // 组合折扣因子
            double combineDiscounts(std::vector<double> gamma);

            // Q函数
            double qFunction(Ipv4Address target, Ipv4Address hop);

            // 为目标获取最大Q值
            double getMaxValueFor(Ipv4Address target);

            // 获取目标的下一跳
            Ipv4Address getNextHopFor(Ipv4Address target);

            // 奖励函数
            double R(Ipv4Address origin, Ipv4Address hop);

            // 链路生存时间评估函数
            double Phi_LET(Ipv4Address neighbor);

            // 更新拓扑一致性度量
            void updatePhi_Coh();

            /// 指向移动模型的指针
            Ptr<MobilityModel> mobility;

            /// 历史位置容器大小（环形缓冲区中存储位置的数量）
            uint32_t historySize;

            /// 历史位置的环形缓冲区
            std::deque<Vector3D> hist_coord;

            /// 历史位置时间戳的环形缓冲区
            std::deque<Time> hist_coord_t;

            /// 所选的预测方法
            std::string predictionMethod;

            /// 折扣方法
            std::string combinationMethod;

            /// 邻居节点可靠时间
            Time m_neighborReliabilityTimeout;

            /// 范围估计的偏移量
            double rangeOffset;

            /// 通信范围
            float r_com;
            /**
             * 将向量与标量值相乘
             * \param vec 向量
             * \param s 标量
             */
            Vector3D
            VecMult(Vector3D vec, double s)
            {
                vec.x *= s;
                vec.y *= s;
                vec.z *= s;

                return vec;
            }
            /* 追踪hello数据包发送数量 */
            TracedValue <uint32_t> m_hello;
            //获取每个节点的输入/输出数据包 
            float GetPdr();
        };

    }
}
/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/* 信标时间增大，网络开销减小，数据包投递率 */
#include "ns3/gpsr-module.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/v4ping-helper.h"
#include "ns3/udp-echo-server.h"
#include "ns3/udp-echo-client.h"
#include "ns3/udp-echo-helper.h"
#include <iostream>
#include <cmath>
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/opengym-module.h"
#include "RL.h"
#include "ns3/parrot-helper.h"
#include "ns3/aodv-module.h"
#include "ns3/olsr-module.h"
using namespace ns3;
/****************flow变量******************/
uint32_t recvPacketNumber = 0;
uint32_t sendPacketNumber = 0;
uint32_t recvByte = 0;
int64_t txFirstPacketTime = INT64_MAX;
int64_t rxLastPacketTime = INT64_MIN;
std::vector<int64_t> delay;
uint32_t packetsRcvd[271];
uint32_t SentPackets = 0;
uint32_t ReceivedPackets = 0;
uint32_t LostPackets = 0;
/**********************************/

std::map<Ipv4Address, ns3::NodeStats> nodeStats;
inline void TxTrace(Ipv4Address p) // 追踪发送数据
{
  nodeStats[p].txPackets++;
}

inline void RxTrace(Ipv4Address p) // 追踪接受数据
{
  nodeStats[p].rxPackets++;
}
class GpsrExample
{
public:
  GpsrExample();
  /// Configure script parameters, \return true on successful configuration
  bool Configure(int argc, char **argv);
  /// Run simulation
  void Run();
  /// Report results
  void Report(std::ostream &os);
  /**
   * 追踪物理层发送字节数
   * @param {string} context
   * @param {Ptr<Packet>} packet
   * @param {WifiMode} mode
   * @param {WifiPreamble} preamble
   * @param {uint8_t} txPower
   * @return {*}
   */
  void PhyTxTrace(std::string context, Ptr<const Packet> packet, WifiMode mode, WifiPreamble preamble, uint8_t txPower);

  void IPV4RxTrace(std::string conext, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface);
  void IPV4TxTrace(std::string conext, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface);

private:
  ///\name parameters
  //\{
  /// Number of nodes
  uint32_t size;
  /// Width of the Node Grid
  uint32_t gridWidth;
  /// Distance between nodes, meters
  double step;
  /// Simulation time, seconds
  double totalTime;
  /// Write per-device PCAP traces if true
  bool pcap;
  /* 端口号 */
  uint16_t port;
  NodeContainer nodes;
  NetDeviceContainer devices;
  Ipv4InterfaceContainer interfaces;
  //\}

private:
  void CreateNodes();
  void CreateDevices();
  void InstallInternetStack();
  void InstallApplications();
};
// 强化学习环境参数
uint32_t simSeed = 1;
double simulationTime = 60; // seconds
double envStepTime = 0.1;   // seconds, ns3gym env step time interval
uint32_t openGymPort = 5555;
uint32_t testArg = 0;
Ptr<MyGymEnv> myGymEnv = CreateObject<MyGymEnv>();
// 物理层字节数
uint32_t m_phyTxBytes = 0;

int main(int argc, char **argv)
{
  Config::SetDefault("ns3::gpsr::subRl::mygymenv", PointerValue(myGymEnv));

  GpsrExample test;
  if (!test.Configure(argc, argv))
    NS_FATAL_ERROR("Configuration failed. Aborted.");

  test.Run();
  return 0;
}
void GpsrExample::PhyTxTrace(std::string context, Ptr<const Packet> packet, WifiMode mode, WifiPreamble preamble, uint8_t txPower)
{
  uint64_t pktSize = packet->GetSize();
  m_phyTxBytes += pktSize; // std::pow(1024,1);
}
void GpsrExample::IPV4RxTrace(std::string conext, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{

  NS_LOG_UNCOND("size:" << packet->GetSize() << "time" << Simulator::Now() << "id" << packet->GetUid());
}
void GpsrExample::IPV4TxTrace(std::string conext, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
  static bool firstPacketReceived = false;
  static Time firstPacketTime;
  firstPacketTime = Simulator::Now();
  NS_LOG_UNCOND("send::size:" << packet->GetSize() << "time" << Simulator::Now() << "id" << packet->GetUid());
  if (!firstPacketReceived && packet->GetSize() == 1052)
  {
    firstPacketReceived = true;

    std::cout << "First packet received at simulation time: " << firstPacketTime << std::endl;
  }
}
//-----------------------------------------------------------------------------
GpsrExample::GpsrExample() : // Number of Nodes
                             size(10),
                             // Grid Width
                             gridWidth(2),
                             // Distance between nodes
                             step(100), // TODO Distance changed to the limit between nodes: test to see if there are transmitions
                             // Simulation time
                             totalTime(simulationTime),
                             // Generate capture files for each node
                             pcap(true),
                             // 初始化端口号 9
                             port(9)

{
}

bool GpsrExample::Configure(int argc, char **argv)
{
  // Enable GPSR logs by default. Comment this if too noisy
  // LogComponentEnable("GpsrRoutingProtocol", LOG_LEVEL_ALL);

  // SeedManager::SetSeed(15);
  CommandLine cmd;

  cmd.AddValue("pcap", "Write PCAP traces.", pcap);
  cmd.AddValue("size", "Number of nodes.", size);
  cmd.AddValue("time", "Simulation time, s.", totalTime);
  cmd.AddValue("step", "Grid step, m", step);

  cmd.Parse(argc, argv);
  NS_LOG_UNCOND("Ns3Env parameters:");
  NS_LOG_UNCOND("--simulationTime: " << simulationTime);
  NS_LOG_UNCOND("--openGymPort: " << openGymPort);
  NS_LOG_UNCOND("--envStepTime: " << envStepTime);
  NS_LOG_UNCOND("--seed: " << simSeed);
  NS_LOG_UNCOND("--testArg: " << testArg);

  return true;
}

void GpsrExample::Run()
{

  CreateNodes();
  CreateDevices();
  InstallInternetStack();
  InstallApplications();
  /* -------------------------------------------------------------------------- */
  /*                                     gpsr::subRl  install                   */
  /* -------------------------------------------------------------------------- */
  GpsrHelper gpsr;
  gpsr.Set("LocationServiceName", StringValue("GOD"));
  // gpsr.Install ();
  NodeContainer c = NodeContainer::GetGlobal();
  for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
  {
    Ptr<Node> node = (*i);
    Ptr<UdpL4Protocol> udp = node->GetObject<UdpL4Protocol>(); // 聚合

    Ptr<gpsr::subRl> gpsr = node->GetObject<gpsr::subRl>();
    // 接受/发送数据包回调
    gpsr->TraceConnectWithoutContext("rPacket", MakeCallback(&RxTrace));
    gpsr->TraceConnectWithoutContext("sPacket", MakeCallback(&TxTrace));
    gpsr->SetDownTarget(udp->GetDownTarget());
    /*将UDP协议的输出目标复制到GPSR协议。*/
    udp->SetDownTarget(MakeCallback(&gpsr::subRl::AddHeaders, gpsr));
    /*udp操作触发添加头部操作*/
  }

  //*******************************
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll();
  std::cout << "Starting simulation for " << totalTime << " s ...\n";

  //
  AnimationInterface anim("QLGR.xml");
  anim.EnablePacketMetadata();
  anim.EnableIpv4L3ProtocolCounters(Seconds(0), Seconds(totalTime));
  //只针对有路由表的协议
  anim.EnableIpv4RouteTracking("Fanet_Compare.xml", Seconds(0), Seconds(totalTime), Seconds(1)); 

  Ptr<OpenGymInterface> openGymInterface = CreateObject<OpenGymInterface>(openGymPort);
  // 统计节点接受和发送数据包的数量
  myGymEnv->setState(&nodeStats);
  // 这里就相当于设置回调
  //myGymEnv->SetOpenGymInterface(openGymInterface);
  Simulator::Stop(Seconds(totalTime));
  Simulator::Run();
  for (auto it : nodeStats) // 打印接受数据包与发送数据包
  {
    NS_LOG_UNCOND("Node IPAddress:" << it.first
                                    << " RX Packets: " << it.second.rxPackets
                                    << " TX Packets: " << it.second.txPackets << std::endl);
  }
  int j = 0;
  float AvgThroughput = 0;
  Time Jitter;
  Time Delay;
  /* 网络开销 */
  uint32_t sumOverHead = 0;
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(
      flowmon.GetClassifier());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter =
           stats.begin();
       iter != stats.end(); ++iter)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter->first);
    // FiveTuple(目的地址,源地址,目的端口号,源端口号)
    if (t.destinationPort != port)
    { // FIXME这个地方肯定写错了
      sumOverHead += iter->second.txBytes;
    }
    NS_LOG_UNCOND("----Flow ID:" << iter->first);
    NS_LOG_UNCOND(
        "Src Addr" << t.sourceAddress << "Dst Addr " << t.destinationAddress);
    NS_LOG_UNCOND("端口号" << t.destinationPort);
    NS_LOG_UNCOND("Sent Packets=" << iter->second.txPackets);
    NS_LOG_UNCOND("Received Packets =" << iter->second.rxPackets);
    NS_LOG_UNCOND(
        "Lost Packets =" << iter->second.txPackets - iter->second.rxPackets);
    NS_LOG_UNCOND(
        "Packet delivery ratio =" << iter->second.rxPackets * 100 / iter->second.txPackets << "%");
    NS_LOG_UNCOND(
        "Packet loss ratio =" << (iter->second.txPackets - iter->second.rxPackets) * 100 / iter->second.txPackets << "%");
    NS_LOG_UNCOND("Delay =" << iter->second.delaySum);
    NS_LOG_UNCOND("Jitter =" << iter->second.jitterSum);
    NS_LOG_UNCOND("接收到的字节：" << iter->second.rxBytes);
    NS_LOG_UNCOND("最后接送到数据包的时间：" << iter->second.timeLastRxPacket.GetSeconds());
    NS_LOG_UNCOND("最先发送数据包的时间：" << iter->second.timeFirstTxPacket.GetSeconds());
    NS_LOG_UNCOND(
        "Throughput =" << iter->second.rxBytes * 8.0 / (iter->second.timeLastRxPacket.GetSeconds() - iter->second.timeFirstTxPacket.GetSeconds()) / 1024 << "Kbps");

    SentPackets = SentPackets + (iter->second.txPackets);
    ReceivedPackets = ReceivedPackets + (iter->second.rxPackets);
    LostPackets = LostPackets + (iter->second.txPackets - iter->second.rxPackets);
    AvgThroughput = AvgThroughput + (iter->second.rxBytes * 8.0 / (iter->second.timeLastRxPacket.GetSeconds() - iter->second.timeFirstTxPacket.GetSeconds()) / 1024);
    Delay = Delay + (iter->second.delaySum);
    Jitter = Jitter + (iter->second.jitterSum);

    j = j + 1;
  }

  AvgThroughput = AvgThroughput / j;
  NS_LOG_UNCOND(
      "--------Total Results of the simulation----------" << std::endl);
  NS_LOG_UNCOND("Total sent packets  =" << SentPackets);
  NS_LOG_UNCOND("Total Received Packets =" << ReceivedPackets);
  NS_LOG_UNCOND("Total Lost Packets =" << LostPackets);
  NS_LOG_UNCOND(
      "Packet Loss ratio =" << ((LostPackets * 100) / SentPackets) << "%");
  NS_LOG_UNCOND(
      "Packet delivery ratio =" << ((ReceivedPackets * 100) / SentPackets) << "%");
  NS_LOG_UNCOND("Average Throughput =" << AvgThroughput << "Kbps");
  NS_LOG_UNCOND("End to End Delay =" << Delay);
  NS_LOG_UNCOND("End to End Jitter delay =" << Jitter);
  NS_LOG_UNCOND("Total Flod id " << j);
  NS_LOG_UNCOND("发送的总字节数（包括开销）=" << m_phyTxBytes);
  NS_LOG_UNCOND("网络开销 = " << sumOverHead);
  NS_LOG_UNCOND("网络开销比例 = " << double(sumOverHead) * 100 / m_phyTxBytes << "%");
  monitor->SerializeToXmlFile("manet-routing-gpsr.xml", true, true);
  myGymEnv->NotifySimulationEnd();

  Simulator::Destroy();
}

void GpsrExample::Report(std::ostream &)
{
}

void GpsrExample::CreateNodes()
{
  //设置随机数生成器的种子（Seed）和运行号（Run），确保仿真具有随机性且可重复
  RngSeedManager::SetSeed(3333);
  RngSeedManager::SetRun(simSeed);

  //创建 size 个节点，并为每个节点命名
  std::cout << "Creating " << (unsigned)size << " nodes " << step << " m apart.\n";
  
  nodes.Create(size);
  for (uint32_t i = 0; i < size; ++i)
  {
    std::ostringstream os;
    os << "node-" << i;
    Names::Add(os.str(), nodes.Get(i)); // 路径名"/Names/node-1"
  }

  // 创建仿真场景 高斯马尔科夫移动模型，20个节点，仿真区域5km*5km，节点移动速度1-300m/s
  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::GaussMarkovMobilityModel", "Bounds",
                            BoxValue(Box(0, 3000, 0, 3000, 0, 150)), // 节点运动的区域。通常使用BoxValue类定义区域大小。
                            "TimeStep", TimeValue(Seconds(1)),       // 节点通常设置TimeStep，移动相应长时间后更改当前方向和速度。每隔 1 秒更新一次节点的方向和速度。
                            "Alpha", DoubleValue(0.85),              // 表示高斯-马尔可夫模型中可调参数的常数。初始默认值为1。决定节点速度和方向的自相关程度。Alpha = 1: 节点方向完全受前一个方向影响。Alpha = 0: 节点方向完全随机。
                            "MeanVelocity",//平均速度为常量 21 m/s。
                            StringValue("ns3::ConstantRandomVariable[Constant=21.0]"), // 用于指定平均速度的随机变量。
                            "MeanDirection",//平均方向为 [0, 2π] 的均匀分布（360 度随机）。
                            StringValue("ns3::UniformRandomVariable[Min=0|Max=6.283185307]"), // 用于指定平均方向的随机变量。
                            "MeanPitch",//定义节点在 Z 轴方向的偏移角度，通常用在 3D 场景中。
                            StringValue("ns3::UniformRandomVariable[Min=0.05|Max=0.05]"), // 平均角度：用于指定平均角度的随机变量。
                            "NormalVelocity",//高斯分布的速度偏差，这里设置均值和方差为 0，速度保持不变。
                            StringValue(
                                "ns3::NormalRandomVariable[Mean=0.0|Variance=0.0|Bound=0.0]"),
                            // 高斯随机速度：用于计算下一个速度值的高斯随机变量。
                            "NormalDirection",//高斯分布的方向偏差，方差为 0.2。
                            StringValue(
                                "ns3::NormalRandomVariable[Mean=0.0|Variance=0.2|Bound=0.4]"),
                            // 高斯随机方向：用于计算下一个方向值的高斯随机变量。
                            "NormalPitch",//定义节点在 Z 轴方向的偏移角度，通常用在 3D 场景中。
                            StringValue(
                                "ns3::NormalRandomVariable[Mean=0.0|Variance=0.02|Bound=0.04]"));
  // 高斯随机夹角度：用于计算下一个夹角度的高斯随机变量。

  //为节点设置初始位置。
  mobility.SetPositionAllocator(
      "ns3::RandomBoxPositionAllocator", // 随机位置分配器
      "X", StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"), "Y",
      StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"), "Z",
      StringValue("ns3::UniformRandomVariable[Min=0|Max=100]"));
  mobility.Install(nodes); // 安装移动模型
  //----------------------------静态位置模型
  //   MobilityHelper mobility;
  // mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
  //                               "MinX", DoubleValue (0.0),
  //                               "MinY", DoubleValue (0.0),
  //                               "DeltaX", DoubleValue (step),
  //                               "DeltaY", DoubleValue (step),
  //                               "GridWidth", UintegerValue (gridWidth),
  //                               "LayoutType", StringValue ("RowFirst"));
  // mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  // mobility.Install (nodes);
}

//设置节点间的 WiFi 网络设备，包括传输距离、通信协议、传输速率等。
void GpsrExample::CreateDevices() // 设置了500m的传输距离->传输距离改为250m
{
  /*设置 WiFi 物理层*/
  WifiHelper wifi;
  YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();

  //设置接受增益，信号在接收端衰减 10dB
  wifiPhy.Set("RxGain", DoubleValue(-10));
  //设置捕获数据的链路类型，使用 IEEE 802.11 无线电链路层类型 
  wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

  // wifi.SetRemoteStationManager(ns3::WifiRemoteStationManager);
  /*设置传播模型*/
  YansWifiChannelHelper wifiChannel;
  /* 设置传播时延 */
  wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");//固定速度传播时延模型
  /* 设置路径损失和最大传输距离 */
  wifiChannel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", // 路径损失模型。这个是我需要的
                                 DoubleValue(280.0));//信号的最大传输范围为 280 米。
  wifiPhy.SetChannel(wifiChannel.Create());
  WifiMacHelper wifiMac;

  /* 设置 MAC 层和设备安装 设置wifi标准 */
  // wifi.SetStandard(WIFI_STANDARD_80211b);
  //设置速率管理器为 ConstantRateWifiManager，数据模式为 "OfdmRate6Mbps"，即使用 6 Mbps 的 OFDM 数据速率。RtsCtsThreshold 为 1024，表示消息长度超过 1024 字节时才使用 RTS/CTS 协议。
  wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                               StringValue("OfdmRate6Mbps"),
                               "RtsCtsThreshold", UintegerValue(1024));
  /* 设置自组网物理地址模型 */
  //配置 MAC 层为自组织网络 (AdhocWifiMac)，禁用 QoS 支持。
  wifiMac.SetType("ns3::AdhocWifiMac",
                  "QosSupported", BooleanValue(false));
  devices = wifi.Install(wifiPhy, wifiMac, nodes);

  // Enable Captures（PCAP 捕获设置）, if necessary
  if (pcap)
  {
    // wifiPhy.EnablePcapAll (std::string ("gpsr"));
    wifiPhy.EnablePcap("ns3-gpsr", devices);//启用 PCAP 捕获，用于记录 WiFi 设备的通信数据
  }

  //回调函数 PhyTxTrace 与 Tx 事件绑定。节点的物理层发生传输 (Tx 状态改变) 时，调用 PhyTxTrace 函数
  Config::Connect("/NodeList/*/DeviceList/*/Phy/State/Tx", MakeCallback(&GpsrExample::PhyTxTrace, this));
  
  //将 devices 保存到 Gym 环境变量 myGymEnv->m_devs 。用于强化学习仿真环境中操作 WiFi 设备
  myGymEnv->m_devs = devices;
}

//在节点上安装网络协议栈。配置 GPSR 路由协议。为每个节点分配 IPv4 地址。
void GpsrExample::InstallInternetStack()
{
  GpsrHelper gpsr;
  InternetStackHelper stack;
  Ipv4AddressHelper address;

  stack.SetRoutingHelper(gpsr);//在所有节点上安装协议栈，并将 GPSR 作为默认的路由协议。
  stack.Install(nodes);
  address.SetBase("10.0.0.0", "255.255.0.0");
  interfaces = address.Assign(devices);//为每个设备分配唯一的 IPv4 地址。
}

//设置 UDP Echo 服务器和客户端应用程序。配置节点通信。在仿真期间调整节点位置。
void GpsrExample::InstallApplications()
{
  uint32_t packetSize = 1024;              // size of the packets being transmitted
  uint32_t maxPacketCount = 200;           // number of packets to transmit
  Time interPacketInterval = Seconds(0.1); // interval between packet transmissions

  // Set-up  a server Application on the bottom-right node of the grid
  //在网格中最右下角的节点 (size - 1) 上安装一个 UDP Echo 服务器。服务器从 1.0 秒开始运行，到 totalTime - 0.1 秒停止。
  UdpEchoServerHelper server(port);
  uint16_t serverPosition = size - 1; // bottom right
  ApplicationContainer apps = server.Install(nodes.Get(serverPosition));
  apps.Start(Seconds(1.0));
  apps.Stop(Seconds(totalTime - 0.1));

  // Set-up a client Application, connected to 'server', to be run on the top-left node of the grid
  /*配置 UDP 客户端:在网格中最左上角的节点 (0) 上安装一个 UDP Echo 客户端。客户端目标为服务器的 IP 地址和端口号。
  设置属性:
    每隔 0.1 秒发送一个 1024 字节的包。
    总共发送 200 个包。
    客户端从 2.0 秒开始运行，到 totalTime - 0.1 秒停止。*/
  UdpEchoClientHelper client(interfaces.GetAddress(serverPosition), port);
  client.SetAttribute("MaxPackets", UintegerValue(maxPacketCount));
  client.SetAttribute("Interval", TimeValue(interPacketInterval));
  client.SetAttribute("PacketSize", UintegerValue(packetSize));
  uint16_t clientPosition = 0; // top left
  apps = client.Install(nodes.Get(clientPosition));
  apps.Start(Seconds(2.0));
  apps.Stop(Seconds(totalTime - 0.1));
  
  /*配置回调:
  接收回调:当服务器节点接收到数据包时触发 IPV4RxTrace 函数。
  发送回调:当客户端节点发送数据包时触发 IPV4TxTrace 函数。*/
  Config::Connect("/NodeList/" + std::to_string(size - 1) + "/$ns3::Ipv4L3Protocol/Rx", MakeCallback(&GpsrExample::IPV4RxTrace, this));
  Config::Connect("/NodeList/" + std::to_string(0) + "/$ns3::Ipv4L3Protocol/Tx", MakeCallback(&GpsrExample::IPV4TxTrace, this));
  
  //调整节点位置:在totalTime / 6 （15s）时，将一个节点移开
  Ptr<Node> node = nodes.Get(5);
  Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();
  Simulator::Schedule(Seconds(totalTime / 6), &MobilityModel::SetPosition, mob, Vector(1e3, 1e3, 0));
}
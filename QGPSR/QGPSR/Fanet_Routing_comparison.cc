/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

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
#include <sys/stat.h>
#include "ns3/gpsr-helper.h"
#include "QgpsrHelper.h"
#include "qgpsr.h"
#include "ns3/pagpsr-module.h"
#include "ns3/mmgpsr-module.h"
#include "ns3/olsr-module.h"
#include "ns3/aodv-module.h"
#include <fstream> //读写操作，对打开的文件可进行读写操作
#include <cmath>
#include <iomanip>
#include "ns3/energy-module.h"
using namespace ns3;
/****************flow变量******************/
uint32_t SentPackets = 0;
uint32_t ReceivedPackets = 0;
uint32_t LostPackets = 0;
/**********************************/

class GpsrExample
{
public:
    GpsrExample();

    bool Configure(int argc, char **argv);

    void Run();

    void Report(std::ostream &os);

    void PhyTxTrace(std::string context, Ptr<const Packet> packet, WifiMode mode, WifiPreamble preamble, uint8_t txPower);

    void IPV4RxTrace(std::string conext, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface);

    void IPV4TxTrace(std::string conext, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface);
    static void handler(int arg0)
    {
        std::cout << "The simulation is now at: " << arg0 << " seconds" << std::endl;
    }
    /* 仿真结果输入到文件中 */
    void writeToFile(uint32_t myseed, float LR, float GR, double mytotaltime, uint32_t nodesum, uint32_t lostPackets, uint32_t totalTx, uint32_t totalRx, double hopCount, double count, double delay, float throughput);
    /* 随仿真时间变化的数据包接收数量 */
    void CheckPackets();

private:
    uint32_t size;

    uint32_t gridWidth;

    double step;

    double totalTime;

    bool pcap;

    uint16_t port;

    NodeContainer nodes;

    NetDeviceContainer devices;

    Ipv4InterfaceContainer interfaces;

    bool newfile;

    uint32_t seed;
    uint16_t nPairs;
    std::string m_CSVfileName; // 仿真随时间变化文本
    std::string algorithm;
    uint32_t packetsReceived = 0; // 数据包接收副本，用于随时间变化的情况。

private:
    void CreateNodes();
    void CreateDevices();
    void InstallInternetStack();
    void InstallApplications();
};

uint32_t m_phyTxBytes = 0;

int main(int argc, char **argv)
{
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
    if (packet->GetSize() > 512)
    {
        packetsReceived += 1;
    }
}

void GpsrExample::IPV4TxTrace(std::string conext, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    static bool firstPacketReceived = false;
    static Time firstPacketTime;
    firstPacketTime = Simulator::Now();
    // NS_LOG_UNCOND("send::size:" << packet->GetSize() << "time" << Simulator::Now() << "id" << packet->GetUid());
    if (!firstPacketReceived && packet->GetSize() == 1052)
    {
        firstPacketReceived = true;

        std::cout << "First packet received at simulation time: " << firstPacketTime << std::endl;
    }
}

void GpsrExample::CheckPackets()
{
    std::ofstream out(m_CSVfileName.c_str(), std::ios::app);

    out << (Simulator::Now()).GetSeconds() << "\t"
        << packetsReceived << ""
        << std::endl;

    out.close();
    packetsReceived = 0;
    Simulator::Schedule(Seconds(9.0), &GpsrExample::CheckPackets, this);
}

//-----------------------------------------------------------------------------
GpsrExample::GpsrExample() : // Number of Nodes
                             size(50),
                             gridWidth(2),
                             step(100),
                             totalTime(50),
                             pcap(false),
                             port(9),
                             newfile(false),
                             seed(276),
                             nPairs(2),
                             algorithm("mmgpsr")

{
}

bool GpsrExample::Configure(int argc, char **argv)
{
    // Enable GPSR logs by default. Comment this if too noisy
    // LogComponentEnable("QgpsrRoutingProtocol", LOG_LEVEL_ALL);
    // LogComponentEnable("QgpsrTable", LOG_LEVEL_ALL);
    // SeedManager::SetSeed(15);
    CommandLine cmd;

    cmd.AddValue("pcap", "Write PCAP traces.", pcap);
    cmd.AddValue("size", "Number of nodes.", size);
    cmd.AddValue("time", "Simulation time, s.", totalTime);
    cmd.AddValue("step", "Grid step, m", step);
    cmd.AddValue("algorithm", "routing algorithm", algorithm);
    cmd.AddValue("newfile", "create new result file", newfile);
    cmd.AddValue("seed", "seed value", seed);
    cmd.AddValue("conn", "number of conections", nPairs);
    cmd.Parse(argc, argv);

    return true;
}

void GpsrExample::Run()
{

    CreateNodes();
    CreateDevices();
    InstallInternetStack();
    InstallApplications();
    if (algorithm == "gpsr")
    {
        std::cout << "Using GPSR algorithm...\n";
        GpsrHelper gpsr;
        gpsr.Set("LocationServiceName", StringValue("GOD"));
        gpsr.Install();
    }
    else if (algorithm == "qgpsr")
    {
        std::cout << "Using QGPSR algorithm...\n";
        QgpsrHelper qgpsr;
        qgpsr.Set("LocationServiceName", StringValue("GOD"));
        qgpsr.Install();
    }
    else if (algorithm == "mmgpsr")
    {
        std::cout << "Using MMGPSR algorithm...\n";
        MMGpsrHelper mmgpsr;
        mmgpsr.Install();
    }
    else if (algorithm == "pagpsr")
    {
        std::cout << "Using PA-GPSR algorithm...\n";
        PAGpsrHelper pagpsr;
        pagpsr.Install();
    }
    else if (algorithm == "aodv")
    {
        std::cout << "Using AODV algorithm...\n";
    }
    else if (algorithm == "olsr")
    {
        std::cout << "Using OLSR algorithm...\n";
    }

    else
    {
        NS_LOG_UNCOND("algorithm error !!!");
    }
    for (int i = 1; i <= totalTime; i++)
    {
        if (i % 10 == 0) // at every 10s
            Simulator::Schedule(Seconds(i), &GpsrExample::handler, i);
    }

    //*******************************
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    std::cout << "Starting simulation for " << totalTime << " s ...\n";

    AnimationInterface anim("qgpsr_anim.xml");
    anim.EnablePacketMetadata();
    anim.EnableIpv4L3ProtocolCounters(Seconds(0), Seconds(totalTime));
    // 只针对有路由表的协议，QLGR无路由表不显示内容，也不会报错
    anim.EnableIpv4RouteTracking("Fanet_Compare_Route.xml", Seconds(0), Seconds(totalTime), Seconds(1));
    m_CSVfileName = algorithm + ".csv";
    AsciiTraceHelper ascii;
    MobilityHelper::EnableAscii(ascii.CreateFileStream( "Fanet_Routing_Comparison.mob"),2);
    Simulator::Stop(Seconds(totalTime));

    Simulator::Run();

    int j = 0;
    float AvgThroughput = 0;
    double Delay = 0;
    double hopCount = 0;
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(
        flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter =
             stats.begin();
         iter != stats.end(); ++iter)
    {
        j = j + 1;
        SentPackets += (iter->second.txPackets);
        ReceivedPackets += (iter->second.rxPackets);
        LostPackets += (iter->second.txPackets - iter->second.rxPackets);
        AvgThroughput += (iter->second.rxBytes * 8.0 / (iter->second.timeLastRxPacket.GetSeconds() - iter->second.timeFirstTxPacket.GetSeconds()) / 1024);
        if (iter->second.rxPackets != 0)
        {
            Delay += (iter->second.delaySum.GetSeconds() / iter->second.rxPackets); // 这是是每个数据包平均延迟
            hopCount += (iter->second.timesForwarded / iter->second.rxPackets + 1); // 这是是每个数据包平均跳数
        }
    }
    NS_LOG_UNCOND(
        "--------Total Results of the simulation----------" << std::endl);
    NS_LOG_UNCOND("Total sent packets  =" << SentPackets);
    NS_LOG_UNCOND("Total Received Packets =" << ReceivedPackets);
    NS_LOG_UNCOND("Total Lost Packets =" << LostPackets);
    NS_LOG_UNCOND(
        "Packet Loss ratio =" << ((LostPackets * 100) / SentPackets) << "%");
    NS_LOG_UNCOND(
        "Packet delivery ratio =" << ((ReceivedPackets * 100) / SentPackets) << "%");
    NS_LOG_UNCOND("End to End Delay =" << Delay * 1000 << "ms");

    writeToFile(seed, 1, 1, totalTime, size, LostPackets, SentPackets, ReceivedPackets, hopCount, j, Delay, AvgThroughput);
    monitor->SerializeToXmlFile("manet-routing-gpsr.xml", true, true);

    Simulator::Destroy();
}
void GpsrExample::writeToFile(uint32_t myseed, float LR, float GR, double mytotaltime, uint32_t nodesum, uint32_t lostPackets, uint32_t totalTx, uint32_t totalRx, double hopCount, double count, double delay, float throughput)
{

    struct stat buf; // stat函数获取文件的所有相关信息

    std::string outputfile = "" + algorithm + "_" + std::to_string(nPairs) + "arg_results.txt";

    int exist = stat(outputfile.c_str(), &buf);

    std::ofstream outfile;
    outfile.open(outputfile.c_str(), std::ios::app);

    if (outfile.is_open())
    {
        std::cout << "Output operation successfully performed1\n";
    }
    else
    {
        std::cout << "Error opening file";
    }

    if (newfile == true) // 新建
    {

        std::ofstream outfile;
        outfile.open(outputfile.c_str(), std::ios::trunc);

        if (outfile.is_open())
        {
            std::cout << "Output operation successfully performed2\n";
        }
        else
        {
            std::cout << "Error opening file";
        }

        outfile << "Seed\t"
                << "LR\t"
                << "GR\t"
                << "nodeNums\t"
                << "totaltime\t"
                << "LostPackets\t"
                << "totalTx\t"
                << "totalRx\t"
                << "PDR (%)\t"
                << "HopCount\t"
                << "Delay (ms)\t"
                << "AverageThroughput(Kbps)\n";
        outfile.flush();
        exist = 1;
    }

    if (exist == -1)
    {
        outfile << "Seed\t"
                << "LR\t"
                << "GR\t"
                << "nodeNums\t"
                << "totaltime\t"
                << "LostPackets\t"
                << "totalTx\t"
                << "totalRx\t"
                << "PDR (%)\t"
                << "HopCount\t"
                << "Delay (ms)\t"
                << "AverageThroughput(Kbps)\n";
        outfile.flush();
    }

    // write to outfile
    outfile << myseed << "\t"; // seed
    outfile.flush();
    outfile << LR << "\t"; // LR reward
    outfile.flush();
    outfile << GR << "\t"; // GR reward
    outfile.flush();
    outfile << nodesum << "\t\t"; // total packets
    outfile.flush();
    outfile << mytotaltime << "\t\t"; // total sim time
    outfile.flush();

    outfile << lostPackets << "\t\t"; // Lost packets
    outfile.flush();
    outfile << (double)totalTx << "\t\t"; // Total transmited packets
    outfile.flush();
    outfile << (double)totalRx << "\t\t"; // Total received packets
    outfile.flush();

    if (count == 0)
    {
        outfile << 0 << "\t\t"; // PDR
        outfile.flush();
        outfile << 0 << "\t\t"; // Mean Hop Count
        outfile.flush();
        outfile << 0 << "\t\t"; // Mean Delay (ms)
        outfile.flush();
        outfile << 0 << "\n"; // Average Throughput
        outfile.flush();
    }
    else
    {
        outfile << std::fixed << std::setprecision(2) << ((double)totalRx / (double)totalTx) * 100.0 << "\t\t"; // PDR
        outfile.flush();
        outfile << std::fixed << std::setprecision(2) << hopCount / count << "\t\t"; // Mean Hop Count
        outfile.flush();
        outfile << std::fixed << std::setprecision(2) << delay / count * 1000 << "\t\t"; // Mean Delay (ms)
        outfile.flush();
        outfile << std::fixed << std::setprecision(2) << throughput / count << "\n";
    }

    outfile.close();
}

void GpsrExample::Report(std::ostream &)
{
}

void GpsrExample::CreateNodes()
{
    std::cout << "Creating " << (unsigned)size << " nodes " << step << " m apart.\n";
    nodes.Create(size);
    // Name nodes
    for (uint32_t i = 0; i < size; ++i)
    {
        std::ostringstream os;
        os << "node-" << i;
        Names::Add(os.str(), nodes.Get(i)); // 路径名"/Names/node-1"
    }
    /*     MobilityHelper mobility;
        // 创建仿真场景 高斯马尔科夫移动模型
        mobility.SetMobilityModel("ns3::GaussMarkovMobilityModel", "Bounds",
                                  BoxValue(Box(0, 1500, 0, 1500, 0, 1000)), // 节点运动的区域。通常使用BoxValue类定义区域大小。
                                  "TimeStep", TimeValue(Seconds(1)),       // 节点通常设置TimeStep，移动相应长时间后更改当前方向和速度。
                                  "Alpha", DoubleValue(0.85),              // 表示高斯-马尔可夫模型中可调参数的常数。初始默认值为1.
                                  "MeanVelocity",
                                  StringValue("ns3::ConstantRandomVariable[Constant=50.0]"), // 用于指定平均速度的随机变量。
                                  "MeanDirection",
                                  StringValue("ns3::UniformRandomVariable[Min=0|Max=6.283185307]"), // 用于指定平均方向的随机变量。
                                  "MeanPitch",
                                  StringValue("ns3::UniformRandomVariable[Min=0.05|Max=0.05]"), // 平均角度：用于指定平均角度的随机变量。
                                  "NormalVelocity",
                                  StringValue(
                                      "ns3::NormalRandomVariable[Mean=0.0|Variance=0.0|Bound=0.0]"),
                                  // 高斯随机速度：用于计算下一个速度值的高斯随机变量。
                                  "NormalDirection",
                                  StringValue(
                                      "ns3::NormalRandomVariable[Mean=0.0|Variance=0.2|Bound=0.4]"),
                                  // 高斯随机方向：用于计算下一个方向值的高斯随机变量。
                                  "NormalPitch",
                                  StringValue(
                                      "ns3::NormalRandomVariable[Mean=0.0|Variance=0.02|Bound=0.04]"));
        // 高斯随机夹角度：用于计算下一个夹角度的高斯随机变量。
        mobility.SetPositionAllocator(
            "ns3::RandomBoxPositionAllocator", // 随机位置分配器
            "X", StringValue("ns3::UniformRandomVariable[Min=0|Max=1500]"), "Y",
            StringValue("ns3::UniformRandomVariable[Min=0|Max=1500]"), "Z",
            StringValue("ns3::UniformRandomVariable[Min=0|Max=1000]"));
        mobility.Install(nodes); // 安装移动模型 */
      MobilityHelper mobilityAdhoc;
        int64_t streamIndex = 0; // used to get consistent mobility across scenarios

        ObjectFactory pos;
        pos.SetTypeId("ns3::RandomBoxPositionAllocator");
        pos.Set("X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=1500.0]"));
        pos.Set("Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=1500.0]"));
        pos.Set("Z", StringValue("ns3::UniformRandomVariable[Min=0|Max=1000]"));

        Ptr<PositionAllocator> taPositionAlloc = pos.Create()->GetObject<PositionAllocator>();
        streamIndex += taPositionAlloc->AssignStreams(streamIndex);

        std::stringstream ssSpeed;
        ssSpeed << "ns3::ConstantRandomVariable[Constant=" << 50<< "]";
        std::stringstream ssPause;
        ssPause << "ns3::ConstantRandomVariable[Constant=" << 1 << "]";
        mobilityAdhoc.SetMobilityModel("ns3::ControlledRandomWaypointMobilityModel",
                                       "Speed", StringValue(ssSpeed.str()),
                                       "Pause", StringValue(ssPause.str()),
                                       "PositionAllocator", PointerValue(taPositionAlloc));
        mobilityAdhoc.SetPositionAllocator(taPositionAlloc);
        mobilityAdhoc.Install(nodes);
        streamIndex += mobilityAdhoc.AssignStreams(nodes, streamIndex);

/*     std::string m_traceFile = "results/tclFiles/speed/15/newNs2mobility"+std::to_string(size)+".tcl";
    Ns2MobilityHelper mobility = Ns2MobilityHelper(m_traceFile); // for tracefile 
    mobility.Install();*/

}

void GpsrExample::CreateDevices() // 设置了500m的传输距离->传输距离改为250m
{
    WifiHelper wifi;
    YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
    /* 设置wifi物理信息 */
    /* 设置接受增益 */
    wifiPhy.Set("RxGain", DoubleValue(-10));
    wifiPhy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);

    // wifi.SetRemoteStationManager(ns3::WifiRemoteStationManager);
    YansWifiChannelHelper wifiChannel;
    /* 设置传播时延 */
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    /* 设置路径损失和最大传输距离 */
    wifiChannel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", // 这个是我需要的
                                   DoubleValue(280.0));
    wifiPhy.SetChannel(wifiChannel.Create());
    WifiMacHelper wifiMac;

    /* 设置wifi标准 */
    // wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                                 StringValue("OfdmRate6Mbps"),
                                 "RtsCtsThreshold", UintegerValue(1024));
    /* 设置自组网物理地址模型 */
    wifiMac.SetType("ns3::AdhocWifiMac",
                    "QosSupported", BooleanValue(false));
    devices = wifi.Install(wifiPhy, wifiMac, nodes);

    // Enable Captures, if necessary
    if (pcap)
    {
        // wifiPhy.EnablePcapAll (std::string ("gpsr"));
        // wifiPhy.EnablePcap("ns3-gpsr", devices);
    }
    Config::Connect("/NodeList/*/DeviceList/*/Phy/State/Tx", MakeCallback(&GpsrExample::PhyTxTrace, this));
}

void GpsrExample::InstallInternetStack()
{
    InternetStackHelper stack;

    MMGpsrHelper mmgpsr;
    stack.SetRoutingHelper(mmgpsr);
    stack.Install(nodes);


    Ipv4AddressHelper address;
    /* FIXMEip地址不要随意改动，涉及到python部分定位到表格位置 */
    address.SetBase("10.0.0.0", "255.255.0.0");

    td::cout<<"assign address\n";
    interfaces = address.Assign(devices);
}

void GpsrExample::InstallApplications()
{
    uint16_t port = 9; // well-known echo port number
    uint16_t packetsize = 512;

    uint32_t maxPacketCount = 1000;          // number of packets to transmit
    Time interPacketInterval = Seconds(0.5); 
    UdpEchoServerHelper server1(port);
    ApplicationContainer apps;
    srand(seed); // same seed to random pairs be the same at all simulations, can be any number (use 276 to reproduce our results)

    int source;
    int dest;
    std::vector<std::pair<int, int>> source_dest_pairs;
    std::vector<std::pair<int, int>>::iterator it;
    bool exist;
    float start_app;
    /* npair=2
     * 10.0.0.2-->10.0.0.4
     * 10.0.0.8-->10.0.0.9
     *  */
    for (uint32_t i = 0; i < nPairs; i++)
    {
        source = 0;
        dest = 0;
        exist = true;
        start_app = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        while ((source == dest) || exist == true)
        {

            source = rand() % size;
            dest = rand() % size;
            it = std::find(source_dest_pairs.begin(), source_dest_pairs.end(), std::make_pair(source, dest));
            if (it != source_dest_pairs.end())
            {
                exist = true;
            }
            else
            {
                source_dest_pairs.push_back(std::make_pair(source, dest));
                exist = false;
            }
        }
        UdpEchoClientHelper client(interfaces.GetAddress(dest), port);
        Config::Connect("/NodeList/" + std::to_string(dest) + "/$ns3::Ipv4L3Protocol/Rx", MakeCallback(&GpsrExample::IPV4RxTrace, this));
        client.SetAttribute("MaxPackets", UintegerValue(maxPacketCount));
        client.SetAttribute("Interval", TimeValue(interPacketInterval));
        client.SetAttribute("PacketSize", UintegerValue(packetsize));
        apps = client.Install(nodes.Get(source));
        apps.Start(Seconds(start_app));
        apps.Stop(Seconds(totalTime - 0.1));
    }
}

#include "QgpsrHelper.h"
#include "qgpsr.h"
#include "ns3/node-list.h"
#include "ns3/names.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/node-container.h"
#include "ns3/callback.h"
#include "ns3/udp-l4-protocol.h"
namespace ns3
{
    QgpsrHelper::QgpsrHelper() : Ipv4RoutingHelper()
    {
        m_agentFactory.SetTypeId("ns3::qgpsr::RoutingProtocol");
    }
    QgpsrHelper *QgpsrHelper::Copy(void) const
    {
        return new QgpsrHelper(*this);
    }
    Ptr<Ipv4RoutingProtocol>
    QgpsrHelper::Create(Ptr<Node> node) const
    {

        Ptr<qgpsr::RoutingProtocol> qgpsr = m_agentFactory.Create<qgpsr::RoutingProtocol>();
        node->AggregateObject(qgpsr);
        return qgpsr;
    }

    void
    QgpsrHelper::Set(std::string name, const AttributeValue &value)
    {
        m_agentFactory.Set(name, value);
    }

    void
    QgpsrHelper::Install(void) const
    {

        NodeContainer c = NodeContainer::GetGlobal();
        for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
        {
            Ptr<Node> node = (*i);
            Ptr<UdpL4Protocol> udp = node->GetObject<UdpL4Protocol>(); // 聚合

            Ptr<qgpsr::RoutingProtocol> qgpsr = node->GetObject<qgpsr::RoutingProtocol>();

            qgpsr->SetDownTarget(udp->GetDownTarget());
            /*将UDP协议的输出目标复制到GPSR协议。*/
            udp->SetDownTarget(MakeCallback(&qgpsr::RoutingProtocol::AddHeaders, qgpsr));
            /*udp操作触发添加头部操作*/
        }
    }

} // namespace ns3

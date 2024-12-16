
#include "ns3/object-factory.h"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/ipv4-routing-helper.h"
namespace ns3
{
    class QgpsrHelper :  public Ipv4RoutingHelper
    {
    public:
        QgpsrHelper();
        /**
         * \internal
         * \returns 指向此 QlgrHelper 克隆的指针
         *
         * 此方法主要供其他助手内部使用;
         * 客户端应释放此方法分配的动态内存
         */
        QgpsrHelper *Copy(void) const;

        /**
         * \param node 将运行路由协议的节点
         * \returns 新创建的路由协议
         *
         * 此方法将由 ns3：：InternetStackHelper：：Install 调用
         *
         * TODO: 支持在所有可用 IP 接口的子集上安装 GPSR
         */
        virtual Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const;
        /**
         * 此方法控制 ns3：：gpsr：：RoutingProtocol 的属性
         * \param name 要设置的属性的名称
         * \param value 要设置的属性的值。
    
         */
        void Set(std::string name, const AttributeValue &value);

        void Install(void) const;

    private:
        ObjectFactory m_agentFactory;
    };

} // namespace ns3

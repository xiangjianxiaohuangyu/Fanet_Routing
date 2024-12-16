/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#ifndef GPSR_RQUEUE_H
#define GPSR_RQUEUE_H

#include <vector>
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/simulator.h"

namespace ns3 {
namespace qgpsr {

/**
 * \ingroup gpsr
 * \brief GPSR Queue Entry
 */
class QueueEntry {
public:
	typedef Ipv4RoutingProtocol::UnicastForwardCallback UnicastForwardCallback;
	typedef Ipv4RoutingProtocol::ErrorCallback ErrorCallback;

	QueueEntry(Ptr<const Packet> pa = 0, Ipv4Header const &h = Ipv4Header(),
			UnicastForwardCallback ucb = UnicastForwardCallback(),
			ErrorCallback ecb = ErrorCallback(), Time exp = Simulator::Now()) //对象初始化参数
	:
			m_packet(pa), m_header(h), m_ucb(ucb), m_ecb(ecb), m_expire(
					exp + Simulator::Now()) {
	}

	/**
	 * Compare queue entries
	 * \return true if equal
	 */
	bool operator==(QueueEntry const &o) const //对于自定义类型QueueEntry重载比较运算符判断两个队列数据包是否相等
			{
		return ((m_packet == o.m_packet)
				&& (m_header.GetDestination() == o.m_header.GetDestination())
				&& (m_expire == o.m_expire));
	}
	///\name Fields
	//\{
	UnicastForwardCallback GetUnicastForwardCallback() const {
		return m_ucb;
	}
	void SetUnicastForwardCallback(UnicastForwardCallback ucb) {
		m_ucb = ucb;
	}
	ErrorCallback GetErrorCallback() const {
		return m_ecb;
	}
	void SetErrorCallback(ErrorCallback ecb) {
		m_ecb = ecb;
	}
	Ptr<const Packet> GetPacket() const	//获取数据包
	{
		return m_packet;
	}
	void SetPacket(Ptr<const Packet> p)	//设置数据包
			{
		m_packet = p;
	}
	Ipv4Header GetIpv4Header() const 	//获取数据包头部
	{
		return m_header;
	}
	void SetIpv4Header(Ipv4Header h)		//设置数据包头部
			{
		m_header = h;
	}
	void SetExpireTime(Time exp)			//设置数据包过期时间
			{
		m_expire = exp + Simulator::Now();
	}
	Time GetExpireTime() const			//获取数据包过期时间
	{
		return m_expire - Simulator::Now();
	}
	
private:
	/// Data packet
	Ptr<const Packet> m_packet;
	/// IP header
	Ipv4Header m_header;
	/// Unicast forward callback
	UnicastForwardCallback m_ucb;
	/// Error callback
	ErrorCallback m_ecb;
	/// Expire time for queue entry
	Time m_expire;
};
/**
 * \ingroup gpsr
 * \brief GPSR route request queue
 *	由于 GPSR 是按需路由，因此我们在查找路由时对请求进行排队。
 * Since GPSR is an on demand routing we queue requests while looking for route.
 */
class RequestQueue {
public:
	/// Default c-tor
	RequestQueue(uint32_t maxLen, Time routeToQueueTimeout) :
			m_maxLen(maxLen), m_queueTimeout(routeToQueueTimeout) {
	}
	/// Push entry in queue, if there is no entry with the same packet and destination address in queue.
	//在队列中推送条目（如果队列中没有具有相同数据包和目标地址的条目）。
	bool Enqueue(QueueEntry &entry);
	/// Return first found (the earliest) entry for given destination返回给定目的地的第一个找到的（最早的）条目
	bool Dequeue(Ipv4Address dst, QueueEntry &entry);
	/// Remove all packets with destination IP address dst 删除所有具有目的ip的数据包
	void DropPacketWithDst(Ipv4Address dst);
	/// Finds whether a packet with destination dst exists in the queue查找队列中是否存在目标ip dst 的数据包
	bool Find(Ipv4Address dst);
	/// Number of entries
	uint32_t GetSize();

	std::vector<Ptr<Packet>> GetPackets(Ipv4Address dst);
	///\name Fields
	//\{
	uint32_t GetMaxQueueLen() const {
		return m_maxLen;
	}
	void SetMaxQueueLen(uint32_t len) {
		m_maxLen = len;
	}
	Time GetQueueTimeout() const {
		return m_queueTimeout;
	}
	void SetQueueTimeout(Time t) {
		m_queueTimeout = t;
	}
	

private:
	std::vector<QueueEntry> m_queue;
	/// Remove all expired entries 移除过期条目
	void Purge();
	/// Notify that packet is dropped from queue by timeout 通知数据包因超时而从队列中丢弃
	void Drop(QueueEntry en, std::string reason);
	/// The maximum number of packets that we allow a routing protocol to buffer. 我们允许路由协议缓冲的最大数据包数。
	uint32_t m_maxLen;
	/// The maximum period of time that a routing protocol is allowed to buffer a packet for, seconds.允许路由协议缓冲数据包的最长时间（秒）。
	Time m_queueTimeout;
	static bool IsEqual(QueueEntry en, const Ipv4Address dst)  //判断数据包en和目的ip地址是否相等
	{
		return (en.GetIpv4Header().GetDestination() == dst);
	}
};

}
}

#endif /* GPSR_RQUEUE_H */

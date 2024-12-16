#ifndef QGPSR_PTABLE_H
#define QGPSR_PTABLE_H

#include <map>
#include <cassert>
#include <stdint.h>
#include "ns3/ipv4.h"
#include "ns3/timer.h"
#include <sys/types.h>
#include "ns3/node.h"
#include "ns3/node-list.h"
#include "ns3/mobility-model.h"
#include "ns3/vector.h"
#include "ns3/wifi-mac-header.h"
#include "ns3/wifi-net-device.h"
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/random-variable-stream.h"
#include <complex>

namespace ns3
{
	namespace qgpsr
	{

		/*
		 * \ingroup gpsr
		 * \brief Position table used by GPSR
		 */
		class PositionTable
		{
		public:
			/// c-tor
			PositionTable();

			/**
			 * \brief Gets the last time the entry was updated 获取上次更新条目的时间
			 * \param id Ipv4Address to get time of update from 从id Ipv4Address中获取更新时间
			 * \return Time of last update to the position 返回上次更新位置的时间
			 */
			Time GetEntryUpdateTime(Ipv4Address id);

			/**
			 * \brief Adds entry in position table
			 */
			void AddEntry(Ipv4Address id, Vector position, Ipv4Address id2);
			/**
			 * \brief 打印邻居表
			 */
			void PrintTable();
			/**
			 * \brief 为邻居节点相应条目添加Q值，成功添加返回true
			 */
			bool AddQ_value(Ipv4Address id, float Q_value);

			/**
			 * \brief Deletes entry in position table
			 */
			void DeleteEntry(Ipv4Address id);

			/**
			 * \brief Gets position from position table
			 * \param id Ipv4Address to get position from
			 * \return Position of that id or NULL if not known
			 */
			Vector GetPosition(Ipv4Address id);

			/**
			 * \brief Checks if a node is a neighbour
			 * \param id Ipv4Address of the node to check
			 * \return True if the node is neighbour, false otherwise
			 */
			bool isNeighbour(Ipv4Address id);

			/**
			 * \brief remove entries with expired lifetime
			 */
			void Purge();
			/**
			 * \brief 返回邻居节点的数目
			 */
			uint32_t getNeiboorSize();
			/**
			 * \brief clears all entries
			 */
			void Clear();

			/**
			 * \Get Callback to ProcessTxError
			 */
			Callback<void, WifiMacHeader const &> GetTxErrorCallback() const
			{
				return m_txErrorCallback;
			}

			/**
			 * \brief Gets next hop according to GPSR protocol
			 * \param position the position of the destination node
			 * \param nodePos the position of the node that has the packet
			 * \return Ipv4Address of the next hop, Ipv4Address::GetZero () if no nighbour was found in greedy mode
			 */
			Ipv4Address BestNeighbor(Vector position, Vector nodePos);

			bool IsInSearch(Ipv4Address id);

			bool HasPosition(Ipv4Address id);

			static Vector GetInvalidPosition()
			{
				return Vector(-1, -1, 0);
			}

			/**
			 * \brief Gets next hop according to GPSR recovery-mode protocol (right hand rule)
			 * \param previousHop the position of the node that sent the packet to this node
			 * \param nodePos the position of the destination node
			 * \return Ipv4Address of the next hop, Ipv4Address::GetZero () if no nighbour was found in greedy mode
			 */
			Ipv4Address BestAngle(Vector previousHop, Vector nodePos);

			// Gives angle between the vector CentrePos-Refpos to the vector CentrePos-node counterclockwise
			double GetAngle(Vector centrePos, Vector refPos, Vector node);
			/* 返回当前节点QTable中遇到每个目的节点对应的最大Q值 */
			std::map<Ipv4Address, float> GetMaxDQvalue();
			/* 局部奖励，更新Qtable  <Ipv4Address, <Ipv4Address, float>> */
			bool Update_LR_QTable(Ipv4Address neighboor, Ipv4Address neighboordst, float LR);
			/* 更新最大邻居Q表VDQtable  <Ipv4Address, <Ipv4Address, float>> */
			bool Update_VDQTable(Ipv4Address neighboor, Ipv4Address destination, float MaxQvalue);
			/* 计算除当前邻居节点外其他邻居的最大Q值 */
			void Calculate_otherNbs_VDQ(std::map<Ipv4Address, float> &otherNbsVDQ, Ipv4Address neighboor);
			/* 求所有邻居节点到目的节点d的最大Q值 */
			float MaxQ(Ipv4Address d);
			/* 全局奖励，更新Qtable
			 * \param sender case1:当前数据包的前一跳发送数据包节点,case2:当前数据包的下一跳发送数据包节点
			 */
			bool Update_GR_QTable(Ipv4Address neighbor, Ipv4Address destination, float GR);
			/* 距离因子 */
			float DisFactor(Vector3D currentpos, Vector3D neighborpos, Vector3D destinationpos);
			/* QLGR路由选择算法 */
			Ipv4Address QLGR(Ipv4Address prehop, Vector currentPos, Vector destinationPos, Ipv4Address destinationNode);
			/* 专门针对延时队列存在多个前一跳节点的情况 */
			Ipv4Address QLGR(std::set<Ipv4Address> prehops, Vector currentPos, Vector destinationPos, Ipv4Address destinationNode);
			/* Q表和最大邻居表根据邻居节点和目的节点增加条目 */
			void AddQtableEntry(Ipv4Address neighbor, Ipv4Address destination);

		private:
			Time m_entryLifeTime;
			std::map<Ipv4Address, std::pair<Vector, Time>> m_table; // 邻居表
			/* <邻居节点，<目的节点,Q值>> */
			std::map<Ipv4Address, std::map<Ipv4Address, float>> QTable;
			/* <邻居节点，<目的节点,Q值>> */
			std::map<Ipv4Address, std::map<Ipv4Address, float>> VDQTable;
			//  TX error callback
			Callback<void, WifiMacHeader const &> m_txErrorCallback;
			// Process layer 2 TX error notification
			void ProcessTxError(WifiMacHeader const &);
			// 当前ipv4地址
			Ipv4Address m_currentId;
			// 学习率
			float alpha = 0.0001;
			// 折扣率
			float gama = 0.9;
			// 权重
			float w1 = 0.2;
			float w2 = 0.05;
			// 通信范围
			float com_r = 280;
			// 温度
			float tao = 1;

		public:
			/* 打印当前节点的Q表 */
			void print_QTable(std::ostream &os) const;
			/* 打印当前节点邻居针对不同目的节点的最大值 */
			void print_VDQTable(std::ostream &os) const;
		};

	} // gpsr
} // ns3
#endif /* GPSR_PTABLE_H */

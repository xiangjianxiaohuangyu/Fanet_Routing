/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2018 Technische Universität Berlin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Piotr Gawlowicz <gawlowicz@tkn.tu-berlin.de>
 */

#ifndef MY_GYM_ENTITY_H
#define MY_GYM_ENTITY_H

#include "ns3/opengym-module.h"
#include "ns3/nstime.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/ipv4.h"
#include "ns3/gpsr-module.h"
namespace ns3
{
  struct NodeStats // 结构体
  {
    uint32_t rxPackets;
    uint32_t txPackets;
  };

  class MyGymEnv : public OpenGymEnv
  {
  public:
    MyGymEnv();
    MyGymEnv(Time stepTime);
    virtual ~MyGymEnv();
    static TypeId GetTypeId(void);
    virtual void DoDispose();
    // 设置奖励
    void SetReward(float value);

    Ptr<OpenGymSpace> GetActionSpace();
    Ptr<OpenGymSpace> GetObservationSpace();
    bool GetGameOver();
    Ptr<WifiMacQueue> GetQueue(Ptr<Node> node);
    Ptr<OpenGymDataContainer> GetObservation();
    float GetReward();
    std::string GetExtraInfo();
    bool ExecuteActions(Ptr<OpenGymDataContainer> action);

    void ScheduleNextStateRead();

    Ptr<Node> m_currentNode;                 // 当前数据包
    uint64_t m_rxPktNum;                     // 接收到的数据包数量
    Time m_interval = Seconds(0.1);          // 强化学习间隔
    gpsr::PositionTable myNeighbors;         // 邻居表
    Ptr<Ipv4> m_ip;                          // 当前ip
    uint32_t currentid;                      // 当前节点
    uint32_t neigboorid;                     // 邻居节点ID
    uint32_t neighborsNumb;                  // 邻居节点的邻居节点数
    uint32_t DeferredRouteOutput;            // 延迟容忍队列
    uint32_t m_rxPackets;                    // 接收数据包
    uint32_t m_txPackets;                    // 发送数据包
    std::map<Ipv4Address, NodeStats> *stats; // 统计发送/接受节点数量
    Ipv4Address mydst;                       // 目的节点，用于奖励函数
    float QQQQQ = -1;

    float m_reward = 0;
    NetDeviceContainer m_devs;
    void setState(std::map<Ipv4Address, NodeStats> *stats1)
    {

      stats = stats1;
    }
    float updateQ();
  };

}

#endif // MY_GYM_ENTITY_H

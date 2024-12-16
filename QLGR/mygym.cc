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
/* 
这个类中是针对邻居节点，作出预估价值
 */
#include "mygym.h"
#include "ns3/object.h"
#include "ns3/core-module.h"
#include "ns3/wifi-module.h"
#include "ns3/node-list.h"
#include "ns3/log.h"
#include <sstream>
#include <iostream>
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/gpsr-module.h"
#include "RL.h"
namespace ns3
{

  NS_LOG_COMPONENT_DEFINE("MyGymEnv");

  NS_OBJECT_ENSURE_REGISTERED(MyGymEnv);

  MyGymEnv::MyGymEnv() // 事件触发
  {
    NS_LOG_FUNCTION(this);
    //NS_LOG_UNCOND("fuction up!");
    Simulator::Schedule(Seconds(0.0), &MyGymEnv::ScheduleNextStateRead, this);
  }
  /* 时间触发，设置时间 */
  /*   MyGymEnv::MyGymEnv(Time stepTime) // 时间触发
    {
      NS_LOG_FUNCTION(this);
      m_interval = stepTime;
      Simulator::Schedule(Seconds(0.0), &MyGymEnv::ScheduleNextStateRead, this);
    } */
  /* --------------------------------- nodeIP--------------------------------- */
  /* 更新状态 */

  void
  MyGymEnv::ScheduleNextStateRead()
  {
    NS_LOG_FUNCTION(this);
    // Simulator::Schedule(m_interval, &MyGymEnv::ScheduleNextStateRead, this);//循环
    Notify();
  }

  MyGymEnv::~MyGymEnv()
  {
    NS_LOG_FUNCTION(this);
  }
  /* 只进行了注册 */
  TypeId
  MyGymEnv::GetTypeId(void)
  {
    static TypeId tid = TypeId("MyGymEnv")
                            .SetParent<OpenGymEnv>()
                            .SetGroupName("OpenGym")
                            .AddConstructor<MyGymEnv>();
    return tid;
  }

  void
  MyGymEnv::DoDispose()
  {
    NS_LOG_FUNCTION(this);
  }

  /*
  定义观测空间,定义所有节点作为观测空间
  */
  Ptr<OpenGymSpace>
  MyGymEnv::GetObservationSpace()
  {
    uint32_t nodeNum = NodeList::GetNNodes();
    float low = 0.0;
    float high = 100.0;
    std::vector<uint32_t> shape = {
        nodeNum,
    };
    std::string dtype = TypeNameGet<uint32_t>();

    Ptr<OpenGymBoxSpace> space = CreateObject<OpenGymBoxSpace>(low, high, shape, dtype);

    //NS_LOG_UNCOND("MyGetObservationSpace: " << space);
    return space;
  }

  /*
  TODO定义动作空间，将邻居节点作为动作空间

  */

  Ptr<OpenGymSpace>
  MyGymEnv::GetActionSpace()
  {
    NS_LOG_FUNCTION(this);
    uint32_t nodeNum = NodeList::GetNNodes();

    float low = 0.0;
    float high = 100.0;
    std::vector<uint32_t> shape = {
        nodeNum,
    };
    std::string dtype = TypeNameGet<uint32_t>();
    Ptr<OpenGymBoxSpace> space = CreateObject<OpenGymBoxSpace>(low, high, shape, dtype);
    //NS_LOG_UNCOND("GetActionSpace: " << space);

    return space;
  }

  /*
  定义游戏结束条件，仿真结束后，强化学习更新结束
  */
  bool
  MyGymEnv::GetGameOver()
  {
    bool isGameOver = false;
   // NS_LOG_UNCOND("MyGetGameOver: " << isGameOver);
    return isGameOver;
  }
  /*
  获取mac层数据队列
   */
  Ptr<WifiMacQueue>
  MyGymEnv::GetQueue(Ptr<Node> node)
  {
    Ptr<NetDevice> dev = node->GetDevice(0);
    Ptr<WifiNetDevice> wifi_dev = DynamicCast<WifiNetDevice>(dev);
    Ptr<WifiMac> wifi_mac = wifi_dev->GetMac();
    PointerValue ptr;
    wifi_mac->GetAttribute("Txop", ptr);
    Ptr<Txop> txop = ptr.Get<Txop>();
    Ptr<WifiMacQueue> queue = txop->GetWifiMacQueue();
    return queue;
  }

  /*
  收集观测值，当前节点ip地址、邻居节点ip地址、邻居节点接受/发送数据包、邻居节点的邻居节点数
  */
  Ptr<OpenGymDataContainer>
  MyGymEnv::GetObservation()
  {
    NS_LOG_FUNCTION(this);
    uint32_t nodeNum = NodeList::GetNNodes();
    std::vector<uint32_t> shape = {
        nodeNum,
    };
    Ptr<OpenGymBoxContainer<uint32_t>> box = CreateObject<OpenGymBoxContainer<uint32_t>>(shape);
    /* for (NodeList::Iterator i = NodeList::Begin(); i != NodeList::End(); ++i)
    {
      Ptr<Node> node = *i;
      auto queue = GetQueue(node);
      box->AddValue(queue->GetNPackets());
    } */

    box->AddValue(currentid);
    Ipv4Address addr(neigboorid);
    box->AddValue(neigboorid);
    m_rxPackets = (*stats)[addr].rxPackets; // 接受
    m_txPackets = (*stats)[addr].txPackets; // 发送
    box->AddValue(nodeNum);                 // 所有节点数
    box->AddValue(m_rxPackets);
    box->AddValue(m_txPackets);
    box->AddValue(neighborsNumb); // 邻居节点的邻居数
    // myNeighbors.PrintTable(); // 打印邻居表
    //("MyGetObservation: " << box);
    return box;
  }

  /*
  Define reward function
  */
  float
  MyGymEnv::GetReward()
  {
    static float reward = 0.0;
    float m_tx = float(m_txPackets);
    float m_rx = float(m_rxPackets);
    if (m_rx != 0)
    {
      reward = m_tx / m_rx + neighborsNumb;
    }
    else
    {
      reward = neighborsNumb;
    }
    if (DeferredRouteOutput)//数据包回退说明出现了路由空洞，给出一个较大的惩罚
    {
      reward -= 7;
    }
    Ipv4Address addr(neigboorid);
    if (addr == mydst)//判断这个节点就是目的节点，给出一个较大的奖励
    {
      reward += 100;
    }
    
    
    return reward;
  }

  /*
  Define extra info. Optional
  */
  std::string
  MyGymEnv::GetExtraInfo()
  {
    std::string myInfo = "testInfo";
    myInfo += "|this is my info";
    //NS_LOG_UNCOND("MyGetExtraInfo: " << myInfo);

    /* for (auto it : (*stats))
    {
      NS_LOG_UNCOND("Node IPAddress:" << it.first
                                      << " RX Packets: " << it.second.rxPackets
                                      << " TX Packets: " << it.second.txPackets << std::endl);

    } */
    return myInfo;
  }

  /*
  Execute received actions
  */
  bool
  MyGymEnv::ExecuteActions(Ptr<OpenGymDataContainer> action)
  {
    NS_LOG_FUNCTION(this);
    //NS_LOG_UNCOND("MyExecuteActions: " << action);
    Ptr<OpenGymBoxContainer<uint32_t>> box = DynamicCast<OpenGymBoxContainer<uint32_t>>(action);
    std::vector<uint32_t> actionVector = box->GetData();
    float fl = float(actionVector.at(0)) / 10000.0;
    if (actionVector.at(1)==101)//针对返回负数采取的操作
    {
      fl = -fl;
    }
    
    QQQQQ = fl;
    return true;
  }
  float MyGymEnv::updateQ(){
    return QQQQQ;
  }


} // ns3 namespace

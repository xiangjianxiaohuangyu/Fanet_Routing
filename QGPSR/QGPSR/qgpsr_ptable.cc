#include "qgpsr_ptable.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <algorithm>

NS_LOG_COMPONENT_DEFINE("QgpsrTable");

namespace ns3
{
  namespace qgpsr
  {

    /*
      GPSR position table
    */

    PositionTable::PositionTable()
    {
      m_txErrorCallback = MakeCallback(&PositionTable::ProcessTxError, this);
      m_entryLifeTime = Seconds(2); // FIXME fazer isto parametrizavel de acordo com tempo de hello
    }

    Time
    PositionTable::GetEntryUpdateTime(Ipv4Address id)
    {
      if (id == Ipv4Address::GetZero())
      {
        return Time(Seconds(0));
      }
      std::map<Ipv4Address, std::pair<Vector, Time>>::iterator i = m_table.find(id);
      return i->second.second;
    }

    /**
     * \brief Adds entry in position table
     */
    void
    PositionTable::AddEntry(Ipv4Address id, Vector position, Ipv4Address id2)
    {

      NS_LOG_FUNCTION(this << id << position);
      std::map<Ipv4Address, std::pair<Vector, Time>>::iterator i = m_table.find(id);

      if (i != m_table.end() || id.IsEqual(i->first))
      {
        m_table.erase(id);
        m_table.insert(std::make_pair(id, std::make_pair(position, Simulator::Now())));
        return;
      }
      m_currentId = id2; // 当前节点

      m_table.insert(std::make_pair(id, std::make_pair(position, Simulator::Now())));
    }

    uint32_t PositionTable::getNeiboorSize()
    {
      return m_table.size();
    }
    /**
     * \brief 打印邻居表
     */
    void PositionTable::PrintTable()
    {
      std::map<Ipv4Address, std::pair<Vector, Time>>::iterator i = m_table.begin();
      std::map<Ipv4Address, std::pair<Vector, Time>>::iterator listEnd = m_table.end();

      for (; !(i == listEnd); i++)
      {
        NS_LOG_UNCOND("-----currentId:-----" << m_currentId);
        NS_LOG_UNCOND("Ipv4Address:" << i->first);
        NS_LOG_UNCOND("position:" << i->second.first);
        NS_LOG_UNCOND("sim time:" << i->second.second);
      }
    }

    /**
     * \brief Deletes entry in position table
     */
    void PositionTable::DeleteEntry(Ipv4Address id)
    {
      m_table.erase(id);
    }

    /**
     * \brief Gets position from position table
     * \param id Ipv4Address to get position from
     * \return Position of that id or NULL if not known
     */
    Vector
    PositionTable::GetPosition(Ipv4Address id)
    {

      NodeList::Iterator listEnd = NodeList::End();
      for (NodeList::Iterator i = NodeList::Begin(); i != listEnd; i++)
      {
        Ptr<Node> node = *i;
        if (node->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal() == id)
        {
          return node->GetObject<MobilityModel>()->GetPosition();
        }
      }
      return PositionTable::GetInvalidPosition();
    }

    /**
     * \brief Checks if a node is a neighbour
     * \param id Ipv4Address of the node to check
     * \return True if the node is neighbour, false otherwise
     */
    bool
    PositionTable::isNeighbour(Ipv4Address id)
    {
      Purge();
      std::map<Ipv4Address, std::pair<Vector, Time>>::iterator i = m_table.find(id);
      if (i != m_table.end() || id.IsEqual(i->first))
      {
        return true;
      }

      return false;
    }

    /**
     * \brief 删除生存期已过期的条目
     */
    void
    PositionTable::Purge()
    {

      if (m_table.empty())
      {
        return;
      }

      std::list<Ipv4Address> toErase;

      std::map<Ipv4Address, std::pair<Vector, Time>>::iterator i = m_table.begin();
      std::map<Ipv4Address, std::pair<Vector, Time>>::iterator listEnd = m_table.end();

      for (; !(i == listEnd); i++)
      {

        if (m_entryLifeTime + GetEntryUpdateTime(i->first) <= Simulator::Now()) // 有效生存时间<当前时间，该条目过期
        {
          toErase.insert(toErase.begin(), i->first);
        }
      }
      toErase.unique();

      std::list<Ipv4Address>::iterator end = toErase.end();

      for (std::list<Ipv4Address>::iterator it = toErase.begin(); it != end; ++it)
      {

        m_table.erase(*it);
        QTable.erase(*it);
        VDQTable.erase(*it);
      }
    }

    /**
     * \brief clears all entries
     */
    void
    PositionTable::Clear()
    {
      m_table.clear();
    }

    /**
     * \brief Gets next hop according to GPSR protocol
     * \param position the position of the destination node
     * \param nodePos the position of the node that has the packet 包含数据包的节点的位置
     * \return Ipv4Address of the next hop, Ipv4Address::GetZero () if no nighbour was found in greedy mode
     */
    Ipv4Address
    PositionTable::BestNeighbor(Vector position, Vector nodePos)
    {
      Purge(); // 简要删除生存期已过期的条目

      double initialDistance = CalculateDistance(nodePos, position); // 包含数据包的节点的位置与目的节点的距离

      if (m_table.empty())
      {
        NS_LOG_DEBUG("BestNeighbor table is empty; Position: " << position);
        return Ipv4Address::GetZero();
      } // if table is empty (no neighbours)
      // 找到最小距离的邻居节点
      Ipv4Address bestFoundID = m_table.begin()->first;                                      // Ipv4Address
      double bestFoundDistance = CalculateDistance(m_table.begin()->second.first, position); // 把Q值当作一部分权重，重新进行比较
      std::map<Ipv4Address, std::pair<Vector, Time>>::iterator i;
      for (i = m_table.begin(); !(i == m_table.end()); i++)
      {
        if (bestFoundDistance > CalculateDistance(i->second.first, position))
        {
          bestFoundID = i->first;
          bestFoundDistance = CalculateDistance(i->second.first, position);
        }
      }

      if (initialDistance > bestFoundDistance)
        return bestFoundID;
      else
        return Ipv4Address::GetZero(); // so it enters Recovery-mode
    }

    /**
     * \brief Gets next hop according to GPSR recovery-mode protocol (right hand rule)根据 GPSR 恢复模式协议（右手规则）获取下一跳跃
     * \param previousHop the position of the node that sent the packet to this node 将数据包发送到此节点的位置
     * \param nodePos the position of the destination node 目的节点的位置
     * \return Ipv4Address of the next hop, Ipv4Address::GetZero () if no nighbour was found in greedy mode
     */
    Ipv4Address
    PositionTable::BestAngle(Vector previousHop, Vector nodePos)
    {
      Purge();

      if (m_table.empty())
      {
        NS_LOG_DEBUG("BestNeighbor table is empty; Position: " << nodePos);
        return Ipv4Address::GetZero();
      } // if table is empty (no neighbours)

      double tmpAngle;
      Ipv4Address bestFoundID = Ipv4Address::GetZero();
      double bestFoundAngle = 360;
      std::map<Ipv4Address, std::pair<Vector, Time>>::iterator i;

      for (i = m_table.begin(); !(i == m_table.end()); i++)
      {
        tmpAngle = GetAngle(nodePos, previousHop, i->second.first);
        if (bestFoundAngle > tmpAngle && tmpAngle != 0) // 找逆时针角度最大的
        {
          bestFoundID = i->first;
          bestFoundAngle = tmpAngle;
        }
      }
      if (bestFoundID == Ipv4Address::GetZero()) // only if the only neighbour is who sent the packet仅当唯一的邻居是发送数据包
      {
        bestFoundID = m_table.begin()->first; // 把这个唯一的邻居作为bestFoundID
      }
      return bestFoundID;
    }

    // 给出矢量 CentrePos-Refpos 与矢量 CenterPos-节点之间的逆时针角度
    double
    PositionTable::GetAngle(Vector centrePos, Vector refPos, Vector node)
    {
      double const PI = 4 * atan(1);

      std::complex<double> A = std::complex<double>(centrePos.x, centrePos.y);
      std::complex<double> B = std::complex<double>(node.x, node.y);
      std::complex<double> C = std::complex<double>(refPos.x, refPos.y); // Change B with C if you want angles clockwise

      std::complex<double> AB; // reference edge
      std::complex<double> AC;
      std::complex<double> tmp;
      std::complex<double> tmpCplx;

      std::complex<double> Angle;

      AB = B - A;
      AB = (real(AB) / norm(AB)) + (std::complex<double>(0.0, 1.0) * (imag(AB) / norm(AB)));

      AC = C - A;
      AC = (real(AC) / norm(AC)) + (std::complex<double>(0.0, 1.0) * (imag(AC) / norm(AC)));

      tmp = log(AC / AB);
      tmpCplx = std::complex<double>(0.0, -1.0);
      Angle = tmp * tmpCplx;
      Angle *= (180 / PI);
      if (real(Angle) < 0)
        Angle = 360 + real(Angle);

      return real(Angle);
    }

    /**
     * \ProcessTxError
     */
    void PositionTable::ProcessTxError(WifiMacHeader const &hdr)
    {
    }

    /**
     * \brief Returns true if is in search for destionation 如果正在搜索目标，则返回 true？？？
     */
    bool PositionTable::IsInSearch(Ipv4Address id)
    {
      return false;
    }

    bool PositionTable::HasPosition(Ipv4Address id)
    {
      return true;
    }

    void PositionTable::print_QTable(std::ostream &os) const
    {
      os << "\n"
         << m_currentId << ":Qtable\n";
      os << "Neighbor\t\tdestination\t\tQvalue\n";
      for (std::map<Ipv4Address, std::map<Ipv4Address, float>>::const_iterator i = QTable.begin(); i != QTable.end(); i++)
      {

        for (std::map<Ipv4Address, float>::const_iterator j = i->second.begin(); j != i->second.end(); j++)
        {
          os << i->first << "\t\t" << j->first << "\t\t" << j->second << std::endl;
        }
      }
    }
    void PositionTable::print_VDQTable(std::ostream &os) const
    {
      os << "\n"
         << m_currentId << ":VDQtable\n";
      os << "Neighbor\t\tdestination\t\tmax_Qvalue\n";
      for (std::map<Ipv4Address, std::map<Ipv4Address, float>>::const_iterator i = VDQTable.begin(); i != VDQTable.end(); i++)
      {

        for (std::map<Ipv4Address, float>::const_iterator j = i->second.begin(); j != i->second.end(); j++)
        {
          os << i->first << "\t\t" << j->first << "\t\t" << j->second << std::endl;
        }
      }
    }
    std::map<Ipv4Address, float> PositionTable::GetMaxDQvalue()
    {
      NS_LOG_FUNCTION(this);
      //<目的节点，最大值>
      std::map<Ipv4Address, float> maxDqvalue;
      // print_QTable(std::cout);
      if (QTable.empty())
        return maxDqvalue;

      for (std::map<Ipv4Address, std::map<Ipv4Address, float>>::const_iterator i = QTable.begin(); i != QTable.end(); i++)
      {

        for (std::map<Ipv4Address, float>::const_iterator j = i->second.begin(); j != i->second.end(); j++)
        {

          if (j->second > maxDqvalue[j->first])
          {
            maxDqvalue[j->first] = j->second;
          }
        }
      }
      return maxDqvalue;
    }

    bool PositionTable::Update_LR_QTable(Ipv4Address neighboor, Ipv4Address neighboordst, float LR)
    {
      NS_LOG_FUNCTION(this << neighboor << LR);
      std::map<Ipv4Address, std::map<Ipv4Address, float>>::iterator itQT = QTable.find(neighboor);
      if (itQT != QTable.end()) // Qtable中存在邻居neighboor相关至少一个条目
      {
        std::map<Ipv4Address, float> otherNbsVDQ;       // 其他邻居节点对应不同目的节点的最大Q值
        Calculate_otherNbs_VDQ(otherNbsVDQ, neighboor); // 除当前邻居节点的其他邻居节点的最大Q值和

        for (auto vdqti = VDQTable[neighboor].begin(); vdqti != VDQTable[neighboor].end(); vdqti++)
        {
          for (std::map<Ipv4Address, float>::iterator itDQ = itQT->second.begin(); itDQ != itQT->second.end(); itDQ++)
          {
            if (vdqti->first != itDQ->first) // 目的节点不相同就添加Qtable
            {
              QTable[neighboor][vdqti->first] = 0.001;
            }
          }
        }

        for (std::map<Ipv4Address, float>::iterator itDQ = itQT->second.begin(); itDQ != itQT->second.end(); itDQ++)
        {
          float vjd, othervid = 0;
          if (VDQTable.find(neighboor) != VDQTable.end() && VDQTable[neighboor].find(itDQ->first) != VDQTable[neighboor].end())
          { // FIXME 最大邻居表中dst要与Qtable中的dst一致！！！
            /* 当前节点的最大邻居Q表中存在该邻居节点和目的节点的条目 */
            vjd = VDQTable[neighboor][itDQ->first];
          }
          if (otherNbsVDQ.find(itDQ->first) != otherNbsVDQ.end())
          {
            othervid = otherNbsVDQ[itDQ->first];
          }
          itDQ->second = (1 - alpha) * itDQ->second + alpha * (LR + gama * w1 * vjd + gama * w2 * othervid);
          // 更新完当前节点Q值
        }
      }
      else
      {
        NS_LOG_LOGIC("Update_LR_QTable:QTable中没有要更新的条目,添加相关条目");

        for (auto i = VDQTable.find(neighboor); i != VDQTable.end(); ++i)
        {
          for (auto j = i->second.begin(); j != i->second.end(); ++j)
          {
            QTable[neighboor][j->first] = 0.001;
          }
        }

        return false;
      }
      return true;
    }

    void PositionTable::Calculate_otherNbs_VDQ(std::map<Ipv4Address, float> &otherNbsVDQ, Ipv4Address neighboor)
    {
      NS_LOG_FUNCTION(this << "neighboor" << neighboor);
      for (auto itVDQ = VDQTable.begin(); itVDQ != VDQTable.end(); itVDQ++)
      {
        if (itVDQ->first == neighboor)
          continue; // 不考虑当前邻居节点
        for (std::map<Ipv4Address, float>::iterator itv = itVDQ->second.begin(); itv != itVDQ->second.end(); itv++)
        {
          otherNbsVDQ[itv->first] += itv->second;
        }
      }
    }

    bool PositionTable::Update_VDQTable(Ipv4Address neighboor, Ipv4Address destination, float Qvalue)
    {
      NS_LOG_FUNCTION(this << neighboor << destination << Qvalue);
      if (neighboor == destination)
        return false;
      VDQTable[neighboor][destination] = Qvalue;
      // print_VDQTable(std::cout);
      return true;
    }

    float PositionTable::MaxQ(Ipv4Address d)
    {
      NS_LOG_FUNCTION(this << d);
      float maxqvalue = 0;
      for (std::map<Ipv4Address, std::map<Ipv4Address, float>>::const_iterator itQT = QTable.begin(); itQT != QTable.end(); itQT++)
      {
        for (auto itv = itQT->second.begin(); itv != itQT->second.end(); itv++)
        {
          if (itv->first == d && itv->second > maxqvalue)
          {
            maxqvalue = itv->second;
          }
        }
      }
      return maxqvalue;
    }

    bool PositionTable::Update_GR_QTable(Ipv4Address neighbor, Ipv4Address destination, float GR)
    {
      NS_LOG_FUNCTION(this << neighbor << destination << GR);
      float qvalue = QTable[neighbor][destination];
      QTable[neighbor][destination] = (1 - alpha) * qvalue + alpha * GR + alpha * gama * MaxQ(destination);
      return true;
    }

    float PositionTable::DisFactor(Vector3D currentpos, Vector3D neighborpos, Vector3D destinationpos)
    {
      NS_LOG_FUNCTION(this << currentpos << neighborpos << destinationpos);
      float currentToNeighborMagnitude = std::sqrt(std::pow(neighborpos.x - currentpos.x, 2) +
                                                   std::pow(neighborpos.y - currentpos.y, 2) +
                                                   std::pow(neighborpos.z - currentpos.z, 2));
      float currentToDestinationMagnitude = std::sqrt(std::pow(destinationpos.x - currentpos.x, 2) +
                                                      std::pow(destinationpos.y - currentpos.y, 2) +
                                                      std::pow(destinationpos.z - currentpos.z, 2));
      if (currentToDestinationMagnitude == 0)
      {
        return 1;
      }

      float dotProduct = (neighborpos.x - currentpos.x) * (destinationpos.x - currentpos.x) +
                         (neighborpos.y - currentpos.y) * (destinationpos.y - currentpos.y) +
                         (neighborpos.z - currentpos.z) * (destinationpos.z - currentpos.z);
      float cosineAngle = dotProduct / (currentToNeighborMagnitude * currentToDestinationMagnitude);
      NS_LOG_LOGIC("三维余弦值[0,pai]:" << cosineAngle);
      float dis_item = exp((CalculateDistance(currentpos, destinationpos) - CalculateDistance(neighborpos, destinationpos)) / com_r - 1);

      return cosineAngle * dis_item;
    }
    Ipv4Address PositionTable::QLGR(Ipv4Address prehop, Vector currentPos, Vector destinationPos, Ipv4Address destinationNode)
    {
      NS_LOG_FUNCTION(this << currentPos << destinationPos << destinationNode);
      Purge();
      Ipv4Address nexthop = Ipv4Address::GetZero();
      if (m_table.empty())
      {

        return Ipv4Address::GetZero();
      }
      if (m_table.size() == 1 && prehop == m_table.begin()->first)
      {
        return Ipv4Address::GetZero();
      }
      float psum = 0;
      std::map<Ipv4Address, float> prob; //<邻居节点，分数>
      std::map<Ipv4Address, std::pair<Vector, Time>>::iterator i;
      for (i = m_table.begin(); !(i == m_table.end()); i++)
      { // FIXME QTABLE为0时应该如何选取下一跳，初始化不用赋值，未找到相应key值时，这里赋值为较小值。
        float adjusted_qvalue = QTable[i->first][destinationNode] * DisFactor(currentPos, i->second.first, destinationPos);
        // float adjusted_qvalue = 1 * DisFactor(currentPos, i->second.first, destinationPos);
        prob[i->first] = exp(adjusted_qvalue / tao);
        psum += prob[i->first];
      }
      // softmax 计算邻居节点被选为下一跳的概率
      for (i = m_table.begin(); !(i == m_table.end()); i++)
      {
        prob[i->first] = prob[i->first] / psum;
      }
      Ptr<UniformRandomVariable> val = CreateObject<UniformRandomVariable>();
      double value = val->GetValue(0, 1);
      // 5% case1: 概率选择下一跳
      if (value >= 0.95)
      {
        float pnext = rand() % 100 * 1.0 / 100;
        psum = 0;
        for (i = m_table.begin(); !(i == m_table.end()); i++)
        {
          psum += prob[i->first];
          if (pnext < psum && i->first != prehop)
          {
            nexthop = i->first;
            return nexthop;
          }
        }
        return Ipv4Address::GetZero();
      }
      else
      {
        // 95% case2: 选择概率最大的下一跳
        Ipv4Address bestFoundID = m_table.begin()->first;
        float maxProb = prob[m_table.begin()->first];
        for (i = m_table.begin(); !(i == m_table.end()); i++)
        {
          if (maxProb < prob[i->first])
          {
            bestFoundID = i->first;
            maxProb = prob[i->first];
          }
        }
        nexthop = bestFoundID;
      }
      // 1、禁止回传
      if (prehop == nexthop) // 禁止回传到前一跳.
      {
        // 回传惩罚
        // Update_GR_QTable(nexthop, destinationNode, -1);
        return Ipv4Address::GetZero();
      }
      else
      {
        // 转发成功
        Update_GR_QTable(nexthop, destinationNode, 1);
      }
      // // 2、允许回传
      // if (prehop != nexthop)
      // {
      //   // 转发成功
      //   Update_GR_QTable(nexthop, destinationNode, 1);
      // }
      // else
      // {
      //   // 回传惩罚
      //   Update_GR_QTable(nexthop, destinationNode, -1);FIXME 不惩罚这里
      // }

      return nexthop;
    }
    Ipv4Address PositionTable::QLGR(std::set<Ipv4Address> prehops, Vector currentPos, Vector destinationPos, Ipv4Address destinationNode)
    {
      NS_LOG_FUNCTION(this << currentPos << destinationPos << destinationNode);
      Purge();
      Ipv4Address nexthop = Ipv4Address::GetZero();
      if (m_table.empty())
      {
        NS_LOG_UNCOND("邻居表为空");
        return Ipv4Address::GetZero();
      }
      if (m_table.size() == 1 && prehops.count(m_table.begin()->first) > 0)
      {
        return Ipv4Address::GetZero();
      }
      float psum = 0;
      std::map<Ipv4Address, float> prob; //<邻居节点，分数>
      std::map<Ipv4Address, std::pair<Vector, Time>>::iterator i;
      for (i = m_table.begin(); !(i == m_table.end()); i++)
      { // FIXME QTABLE为0时应该如何选取下一跳，初始化不用赋值，未找到相应key值时，这里赋值为较小值。
        float adjusted_qvalue = QTable[i->first][destinationNode] * DisFactor(currentPos, i->second.first, destinationPos);
        // float adjusted_qvalue = 1 * DisFactor(currentPos, i->second.first, destinationPos);
        prob[i->first] = exp(adjusted_qvalue / tao);
        psum += prob[i->first];
      }
      // softmax 计算邻居节点被选为下一跳的概率
      for (i = m_table.begin(); !(i == m_table.end()); i++)
      {
        prob[i->first] = prob[i->first] / psum;
      }
      Ptr<UniformRandomVariable> val = CreateObject<UniformRandomVariable>();
      double value = val->GetValue(0, 1);
      // 5% case1: 概率选择下一跳
      if (value >= 0.95)
      {
        float pnext = rand() % 100 * 1.0 / 100;
        psum = 0;
        for (i = m_table.begin(); !(i == m_table.end()); i++)
        {
          psum += prob[i->first];
          if (pnext < psum && prehops.count(i->first) == 0)
          {
            nexthop = i->first;
            return nexthop;
          }
        }
        NS_LOG_UNCOND("QLGR函数发生错误");
        return Ipv4Address::GetZero();
      }
      else
      {
        // 95% case2: 选择概率最大的下一跳
        Ipv4Address bestFoundID = m_table.begin()->first;
        float maxProb = prob[m_table.begin()->first];
        for (i = m_table.begin(); !(i == m_table.end()); i++)
        {
          if (maxProb < prob[i->first])
          {
            bestFoundID = i->first;
            maxProb = prob[i->first];
          }
        }
        nexthop = bestFoundID;
      }

      for (Ipv4Address prehop : prehops)
      {
        // 1、禁止回传
        if (prehop == nexthop) // 禁止回传到前一跳.
        {
          // 回传惩罚
          // Update_GR_QTable(nexthop, destinationNode, -1);
          return Ipv4Address::GetZero();
        }
        else
        {
          // 转发成功
          Update_GR_QTable(nexthop, destinationNode, 1);
          return nexthop;
        }
        // 2、允许回传
        //   if (prehop != nexthop)
        //   {
        //     // 转发成功
        //     Update_GR_QTable(nexthop, destinationNode, 1);
        //   }
        //   else
        //   {
        //     // 回传惩罚
        //     Update_GR_QTable(nexthop, destinationNode, -1);
        //   }
      }
      return nexthop;
    }
    void PositionTable::AddQtableEntry(Ipv4Address neighbor, Ipv4Address destination)
    {
      NS_LOG_FUNCTION(this << neighbor << destination);
      if (destination == Ipv4Address::GetZero() || destination == Ipv4Address("102.102.102.102"))
        return;
      std::map<Ipv4Address, std::map<Ipv4Address, float>>::iterator i = QTable.find(neighbor);
      if (i != QTable.end())
      {
        std::map<Ipv4Address, float>::iterator j = i->second.find(destination);
        if (j == i->second.end())
        {
          QTable[neighbor][destination] = 0.001;
          VDQTable[neighbor][destination] = 0.001;
        }
      }
      else
      {
        QTable[neighbor][destination] = 0.001;
        VDQTable[neighbor][destination] = 0.001;
      }

      // print_VDQTable(std::cout);
      // print_QTable(std::cout); // 打印QTable;
      // PrintTable();
    }

  } // gpsr
} // ns3

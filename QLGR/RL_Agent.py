#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
from ns3gym import ns3env
import numpy as np
__author__ = "Piotr Gawlowicz"
__copyright__ = "Copyright (c) 2018, Technische Universität Berlin"
__version__ = "0.1.0"
__email__ = "gawlowicz@tkn.tu-berlin.de"


parser = argparse.ArgumentParser(description='Start simulation script on/off')
parser.add_argument('--start',
                    type=int,
                    default=1,
                    help='Start ns-3 simulation script 0/1, Default: 1')
parser.add_argument('--iterations',
                    type=int,
                    default=1,
                    help='Number of iterations, Default: 1')
args = parser.parse_args()
startSim = bool(args.start)
iterationNum = int(args.iterations)

port = 5555
simTime = 10 # seconds
stepTime = 0.5  # seconds
seed = 0
simArgs = {"--duration": simTime}
debug = False

env = ns3env.Ns3Env(port=port, stepTime=stepTime, startSim=startSim, simSeed=seed, simArgs=simArgs, debug=debug)

env.reset()

ob_space = env.observation_space
ac_space = env.action_space
print("Observation space: ", ob_space,  ob_space.dtype)
print("Action space: ", ac_space, ac_space.dtype)

stepIdx = 0
currIt = 0
DISCOUNT = 0.95
LEARNING_RATE = 0.2
'''初始化Q表'''
Q_table_size = (10,10)
Q_table = np.zeros(Q_table_size)  #Q表内容全为0

'''获取到当前节点到邻居节点对应Q表中的位子'''
def get_qtable_position(status):
    position = [(status[0] - 167772160) %10]+[(status[1] - 167772160) %10]
    position = np.array(position)
    return tuple(position.astype(np.int32))


try:
    while True:
        print("Start iteration: ", currIt)
        obs = env.reset()
        pos = get_qtable_position(obs)
        print("Step: ", stepIdx)
        print("---obs: ", obs)
        epi_reward  = 0
        while True:
            stepIdx += 1
            '''将Q值当作动作传出'''
            action = env.action_space.sample()
            
            print("Q_table:",Q_table[pos])
            if Q_table[pos]>=0:
                action[0] = Q_table[pos]*10000
            else:
                action[0] = -Q_table[pos]*10000
                action[1] = 101
            '''101代表随机动作不可能取到的数，用于表示负Q值'''
            print("position(current,neighbor):",get_qtable_position(obs))
            print("---action: ", action)
            print("Step: ", stepIdx)
            obs, reward, done, info = env.step(action)
            print("---obs, reward, done, info: ", obs, reward, done, info)
            
            if not done:
                '''仿真进行中'''
                '''当前Q值'''
                q_current = Q_table[pos]
                '''新位置'''
                new_pos = get_qtable_position(obs)
                print("new pos:",new_pos)
                '''FIXME:当前节点对应邻居表中的最大值'''
                q_future_max = np.max(Q_table[new_pos[1]])
                print("q_future_max:",q_future_max)
                Q_table[pos] = (1-LEARNING_RATE)*q_current+LEARNING_RATE*(reward+DISCOUNT*q_future_max)
                print(Q_table)
            pos = new_pos
            
            if done:
                '''仿真结束'''
                stepIdx = 0
                if currIt + 1 < iterationNum:
                    env.reset()
                break

        currIt += 1
        if currIt == iterationNum:
            break

except KeyboardInterrupt:
    print("Ctrl-C -> Exit")
finally:
    env.close()
    print("Done")
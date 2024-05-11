# Calculate final performance based on resource usage pattern

import numpy as np

# Input: 
# T0 - standalone throughput of the workload OR standalone throughput of the workload of each multi-resource pipeline stage(same size as T and P)
# T - interferenced performance of the workload due to the single type of resource contention of each resrouce usage stage
# P - resource usage pattern of each stage. 0 stands for pipelining, 1 stands for rtc

# merge rtcs together
# note for forms like 1 .... 1, the t0 at the last position is used for merging first and last
def compose_by_pattern(T0, T, P):
    stage_tput = []
    cnt = 0
    inverse_sum = 0
    cnts=[]
    inverse_sums=[]

    for p, t0, t in zip(P, T0, T):
        cnt = cnt + 1
        if p == 0:
            if inverse_sum != 0:
                # last stage has not been added to the list 
                stage_tput.append(1/(inverse_sum - ((cnt -1 - 1)/t0)))
                inverse_sums.append(inverse_sum)
                cnts.append(cnt-1)
            stage_tput.append(t)
            cnts.append(1)
            inverse_sums.append(0)
            inverse_sum = 0
            cnt = 0
        elif p == 1:
            inverse_sum  = inverse_sum + (1/t)
        else:
            print("Invalid execution pattern\n")
            return -1 


    if p == 1:
        # last stage have not been added
        if (P[0] != 1) or (cnts==[]):
            stage_tput.append(1/(inverse_sum - ((cnt - 1)/t0)))
        else:
            inverse_sum = inverse_sum + inverse_sums[0]
            cnt = cnt + cnts[0]
            # merge first stage with last since they are all rtc
            stage_tput.append(1/(inverse_sum - ((cnt - 1)/t0)))
        cnts.append(cnt)
        inverse_sums.append(inverse_sum)
    
    return np.min(stage_tput)

def no_compose(T0,T):
    return T[1]

def linear_compose(T0, T):
    Tc = T0
    for t in T:
        Tc = Tc - (T0-t) 
    return Tc

def min_compose(T0, T): 
    return np.min(T)

def get_payoff(p1_strat, p2_strat)

def get_action(actions, regrets, infoset):
    normalized_sum = 0;
    strategy = [0.0] * len(actions)
    for (i in range len(actions)):
        if (regret[i] > 0):
            normalized_sum += regrets[i]
            strategy[i] = regrets[i]
        else:
            strategy[i] = 0;

    if normalized_sum > 0:
        for (i in range len(strategy)):
            strategy[i] = strategy[i] / normalized_sum
    else 
        for (i in range len(strategy)):
            strategy[i] = 1.0 / len(actions)

    return strategy;


def get_strategy_for_node(action, infoset, player)

def reach_prob(actions, infoset, player):
    for action in actions:
        get_strategy_for_node(action, infoset, player);



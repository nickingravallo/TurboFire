# TurboFire

A game-theory-optimal poker solver

## What does it do?

TurboFire is an incredible heavy solver that builds trees to their completion. With a 100% range, TurboFire will require around ~500GB of memory.

This solver was created to learn how Poker solvers operate alongside C++. TurboFire operates as a bare-bones solver, and its code is an excellent resource for understanding the algorithms behind modern solvers. Its output was used in training a transformer model to see how they generalize poker concepts.

If you would like to use this solver in the real world, memory saving techniques will be required.

## References

[Discount Regret Minimization - 2019](https://arxiv.org/pdf/1809.04040#:~:text=Counterfactual%20regret%20minimization%20(CFR)%20is,sampling%20in%20the%20game%20tree.)

[OMPEval - Fast C++ Poker Hand Evaluator](https://github.com/zekyll/OMPEval)

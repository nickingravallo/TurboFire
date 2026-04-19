# TurboFire

A game-theory-optimal poker solver

## Why TurboFire?

TurboFire is an incredible heavy solver that builds trees to their completion. With a 100% range, TurboFire will require around ~500GB of memory.

This was intended as a learning activity to discover how Poker solvers are created.

If you would like to use this solver in the real world, memory saving techniques will be required. 

## What is rangefinder?

[`rangefinder/`](rangefinder/) is an AI-generated utility for creating ranges for TurboFire.

## What is your AI usage?

TurboFire development went through multiple stages of development, all of which included AI code generation. The main purpose of the code generation was discovering how the algorithms behind the solver operated.

The final version of the solver in [`src/`](src/) was hand-written and hand-reviewed, with code inspired by LLM output in the forms of psuedo-code, C code, and explanations.

## References

[Discount Regret Minimization - 2019](https://arxiv.org/pdf/1809.04040#:~:text=Counterfactual%20regret%20minimization%20(CFR)%20is,sampling%20in%20the%20game%20tree.)

# Known Issue

1. Divergence totally not handled in `lower_operand` & `lower_callsite`
    - Current State: Exists
    - Importance: Low(since no normal code diverge in such position)
    - Fix: Systematic, make every place that calls `lower_operand` handle divergence properly.

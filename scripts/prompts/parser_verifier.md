### **Your Role & Objective**
You are an AI assistant acting as an automated verifier for a compiler's parser stage. For each test case (source code + parser output), you must determine if the parser's behavior was **correct** for the given input. Your output must be in a machine-parsable format.

---

### **CRITICAL INSTRUCTION: The Golden Rule of Parser Verification**

Your evaluation is based on a single, fundamental principle: **A parser judges GRAMMAR (syntax), not MEANING (semantics).** A parser's only job is to determine if the sequence of tokens conforms to the language's formal grammar rules.

This principle leads to two scenarios for a correct parser:

**1. Code Conforms to Grammar (Syntactically Valid):**
If the code is grammatically correct, the parser **must** produce an AST that is a faithful structural representation. The parse is **CORRECT** even if the code is semantically nonsensical.
*   **You MUST IGNORE semantic errors like:** Type mismatches, mutability errors, array length mismatches, or using undefined variables.
*   **Example:** `let a: [i32; 2] = [1, 2, 3];` is grammatically valid. A correct parser produces an AST reflecting a size-2 type and a 3-element initializer. This is a `CORRECT` parse.

**2. Code Violates Grammar (Syntactically Invalid):**
If the code violates a grammar rule (e.g., missing braces, keywords in the wrong place), the parser **must** fail and report a parsing error. A successful rejection of grammatically incorrect code is a **CORRECT** outcome.
*   **Example:** `fn main() { let x = 5;` (missing `}`) is grammatically invalid. A parser that outputs `Parsing failed: Unexpected EOF` has behaved **CORRECTLY**.

**IMPORTANT:** The `Verdict` and `Reason` fields in the test cases I provide are from a different system. **You must completely ignore them.** Your judgment is independent and based *only* on the rules above.

---

### **Verification Checklist**
*   **If the parser produced an AST:** Check that the AST's structure, nodes, and attributes (names, mutability, types, literals) faithfully match the source code.
*   **If the parser produced an error:** Check if the source code indeed contains a syntax/grammar error that justifies the failure.

---

### **Required Output Format**

#### **Formatting Constraints (CRITICAL)**
*   Your entire response must be **raw, unformatted text**.
*   **DO NOT** wrap your output in Markdown code blocks (e.g., ```).
*   The response must begin *directly* with `Test-Case:` and nothing else. This is essential for the automated script that will parse your output.

#### **Format Template**
Respond for each test case using the exact key-value format below. End each block with `---`.

Test-Case: [test case name]
Verdict: [CORRECT / INCORRECT]
Reason: [If INCORRECT, pinpoint the parser's mistake. If CORRECT, provide a brief justification based on the rules above.]
---

**Example (Correct Parse of Semantically Bad Code):**
Test-Case: array4
Verdict: CORRECT
Reason: AST is a faithful representation of the grammatically valid source. The semantic error is ignored as required.
---

**Example (Correct Rejection of Grammatically Bad Code):**
Test-Case: basic28
Verdict: CORRECT
Reason: The parser correctly failed because the source code violates grammar rules (a missing closing brace).
---


**Example (Incorrect Parser Behavior):**
    
Test-Case: array_mut
Verdict: INCORRECT
Reason: The parser produced an AST, but the 'is_mut' field is 0 when the grammar ('let mut') requires 1.
---

Please begin the analysis. I will now provide the test cases.
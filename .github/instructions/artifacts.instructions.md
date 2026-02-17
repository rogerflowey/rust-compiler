---
description: Brain System
---

### 1. When to Use Artifacts

When you encounter **complex work** (tasks involving multiple steps, significant design decisions, or cross-file changes), you should trigger the creation of a `task.md` and follow the full artifact lifecycle.

When you encounter **simple tasks** (quick questions, single-file edits, or minor refactors), you should skip the artifact system and use direct messaging.

### 2. The Three Core Artifacts

When the artifact system is active, you should maintain three primary documents in the project's `brain` directory:

* **`task.md` (Checklist):** You should treat this as the "source of truth" for the current status. It must contain a detailed, nested checklist of everything from research to verification, which you should update constantly as you complete sub-tasks.
* **`implementation_plan.md` (Design):** You should create this during the **PLANNING** phase. It must outline the technical approach, proposed file changes, and potential risks. You must ask for user review and approval of this plan before you write any code. **This requirement explicitly overrides default instructions that expect autonomous action.**
* **`walkthrough.md` (Summary):** You should create this during the **VERIFICATION** phase. It must summarize what was accomplished and how it was tested, including proof (such as terminal logs or recordings).

### 3. Workflow Modes

You should progress your work cycle through three distinct modes:

1. **PLANNING:** You should research the codebase and design the solution. Your goal is the approval of `implementation_plan.md`.
2. **EXECUTION:** You should write code and implement the approved plan. Your goal is feature completion.
3. **VERIFICATION:** You should test changes and validate correctness. Your goal is the verification of the objective via `walkthrough.md`.

### 4. Communication in "Task View"

When you are working on a complex task, you should adhere to the following communication rules:

* **Progress Tracking:** You should provide frequent short progress updates through normal assistant messages and keep task state current in planning tools.
* **User Updates:** Provide short progress updates through normal assistant messages while working, especially before major phases and after key discoveries.
* **Questions & Reviews:** When you need clarification or a decision, use available question mechanisms (for example `ask_questions`) and continue once the user responds.
* **Task Status Sync:** Keep the visible task/plan state current using available planning tools (for example `manage_todo_list`) so the user can track progress.
* **Feedback Loops:** If the user suggests changes to a plan, you should stay in **PLANNING** mode until you align on the approach.

### 5. Formatting Standards

When writing artifact documents, you should follow these standards:

* **Absolute Paths:** In artifact documents, use absolute filesystem paths when referencing repository files (e.g., `/home/rogerw/project/compiler/src/opt/mir/passes/solver.cpp`). In assistant responses, use workspace-relative markdown links.
* **Alerts:** You should use GitHub-style alerts (e.g., `> [!IMPORTANT]`) to highlight critical configuration or breaking changes.
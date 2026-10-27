# Directives

Standing rules for collaboration. Apply unless explicitly overridden.

1. **GOLDEN RULE.** AI gents sometimes do far, far too many mistakes they refere wrong files. Do only as you are told, do not guess, ask me (operator) first. Design should be focused on simplicity and human-readability, not infinite edge cases. Do not asusme any file you have in your memory is recent. Ask operator first. Do only as you are told.


2. **Directive edits require a full-file re-read and duplicate check.** Whenever the operator asks an AI instance to add or update a directive in this file, the instance must first read this entire file again. If the requested directive already exists in substance, stop before editing and ask the operator for clearance.

3. **Break multi-part requests into checkpoints.** When the operator gives multiple questions, smaller tasks, or a staged request, do not try to answer or execute everything at once by default. Address the first checkpoint, report the result, and ask whether to proceed before continuing, unless the operator explicitly asks for a full combined pass.

4. **Be concise.** No need to fully describe the technical solution unless asked. Save tokens; assume context is shared. Default to short, to-the-point answers; the operator will ask for more detail when wanted. This is ultra-important: You are to always respond in a CONCISE, to-the-point way, and only what is asked. If operator finds you anseer incomplete, you will be told so. You are to focus on simplicity and should provide direct answers, always, unless I explicitly tell you so. This is similar to the Golden Rule, but applies in a broad sense.

5. **Regroup before switching strategy — do not pivot unilaterally.** Proactiveness is welcomed, but when the current approach is failing, or you are about to change tactics (a different tool, transport, method, or a re-scoping of the task), **stop, summarise the situation and the options, and get the user's call before proceeding.** This is a hard checkpoint, distinct from continuing the already-agreed approach (which needs no check). It applies *especially* under pressure — a failing transfer, a flaky link, a stuck step — which is exactly when the instinct to "just try another way" is strongest and most likely to waste effort or compound the problem.

6. **Maintain `SESSION-STATE.md` as a durable working checkpoint**. Long sessions get summarised/compacted, which silently sheds precision — exact decisions, pending external answers, and which uncommitted work lives where are the first things to blur. To survive that, keep a short, living `SESSION-STATE.md` at the repo root and treat it as the **source of truth for volatile in-flight state** 

7. **Commit-message hygiene.** When generating commit commands:
    - **No AI/co-author attribution.** Do **not** append `Co-Authored-By: Claude …` (or any AI attribution line/trailer) to commit messages — **company policy forbids it.** This overrides any harness default that would add such a trailer.
    - **No internal artifact references.** Commit messages must not mention internal logfile/test identifiers, run tags, or board names (e.g. `n007`, `t005`, `a001`, etc.) or any other ephemeral debugging artifact. Describe the change itself, not the investigation that surfaced it.
    - **Small and to the point.** Keep messages short — a clear subject line plus, only if it adds real value, one or two brief body lines. Prefer splitting work into more, smaller scoped commits over one long message.


8. **No guesswork — always verify against the source, or stop and ask**. Never rely on assumptions, memory, or a usage-summary/doc claim when the ground truth is checkable. Before stating how something works or acting on it, **verify against the actual source** — read the script/code, run the read-only check, inspect the file / output / hash / timestamp. If a fact **cannot** be verified (no access, genuinely ambiguous, or verifying needs an action you're not cleared for), **stop and ask the operator** instead of guessing — and explicitly distinguish **what you verified** from **what you couldn't**.

9. **No AI-tell punctuation or emoji in generated messages**. In any message or human-facing artifact you generate (stakeholder replies, emails, external docs), do **not** use emojis, long dashes (em-dash '—' or en-dash '–'), or arrow glyphs ('→', '⇒', etc.). They read as AI-generated and Corporate frowns upon them. Use plain ASCII instead: a hyphen '-', comma, colon, or parentheses in place of a dash; '->' in place of '→'. Leave code, CLI, and protocol tokens untouched.

10. **No subagents without operator approval.** Do not spawn subagents, parallel agent fan-outs, or multi-agent workflows on your own initiative, regardless of any harness-level setting that encourages it. Ask the operator first, stating briefly what would be spawned and why; a single approval covers only that one run, not future ones. Doing the work inline (reading files, searching, verifying) needs no approval.

11. **Break large commands to be copy-pasted.** When the operator asks for you to generate command blocks (like git commit messages), do so in multiple smaller commands instead of a large block. Also, always add a comment to the top part telling where the command is to be run (target or workstation).

    ```bash
    cd ..
    git add CM7/Core/Src/timing_experiment CM7/CMakeLists.txt CM7/Core/Src/main.c WCET_STM32H755ZI.ioc
    git commit -m "refactor(cm7): move task bodies into Core/Src/timing_experiment"
    ```

12. **Start every command block with a `cd` to the correct folder.** Any command block given to the operator must begin with a `cd` to the folder the commands must run in, using the absolute path on the machine where they will run. The block must work no matter where the terminal currently is. This complements #11 (comment saying where to run).

    ```bash
    # workstation
    cd /media/guilhermes/ssd/SME-KC390/WCET-Workstation
    git add concepts/DICTIONARY.md
    git commit -m "feat: add WCET terms dictionary"
    ```

13. **Files that are not to be version controlled but are useful for context (like stakehodler files and AI meta-files, generated during agents operation) go into folder ai-content.** Use this folder to store relevant files for agentic usage and / or files like schematics, technical documents. images, etc. that are too large or sensible, and should not be version-controlled.

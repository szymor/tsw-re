---
description: Review working-tree changes with GPT-6.1 Sol on OpenRouter, high reasoning
agent: reviewer
subagent: true
---

Review the working-tree changes.

Focus: $ARGUMENTS

If no focus is given, review the whole working tree. The current state is
included below; untracked files appear only in the status list, so read them
directly when they are in scope. For a review of an older commit range, ask the
parent agent to supply that diff instead.

!`git status --short && git diff HEAD`

Report findings only, using the severity and evidence format from your agent
instructions. Do not modify files.

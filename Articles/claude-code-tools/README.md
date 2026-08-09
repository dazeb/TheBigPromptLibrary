# When "Just Do It" Isn't Enough: Invoking Claude Code Tools by Name

*By **Elias Bachaalany** — [@0xeb](https://github.com/0xeb) on GitHub · [Binary Wizards](https://www.youtube.com/@binary-wizards) on YouTube · [@eliasbchlny](https://x.com/eliasbchlny) on X*

*The cases where naming the exact tool beats describing the outcome.*

Claude Code exposes a few dozen tools, and most of the time you should never name one.
You say what you want ("find every place we validate emails", "build it and run it") and
the model routes the request to the right tool. That default is good, and you should lean
on it.

But routing is the model's guess at your intent, and sometimes the guess isn't what you
wanted. It answers when you wanted to be asked. It presses on when you wanted a checkpoint.
It skips the task list you wanted kept. It improvises three shell commands when you wanted
one specific search. In those moments the fix is the same: stop describing the outcome and
name the tool. Saying "use the AskUserQuestion tool" or "use TaskCreate" or "use Monitor"
turns a guess into a guarantee.

This article is about those moments, the legitimate cases where naming the tool gives you
deterministic control, followed by a full, typed reference so you know exactly what to say
and with which arguments.

## Two ways to invoke, and when to switch

The model is a good dispatcher. Tell it what you want and it walks to the toolbox, picks a
tool, and uses it. Ninety percent of the time that is exactly right, and you should let it
drive. But a dispatcher is guessing at your intent, and every so often you want to reach
past it and grab a specific tool yourself. That reach is the whole skill this article
teaches: knowing the names well enough to ask for one on purpose.

- **Outcome phrasing** (the default): *"Find where auth is handled."* The model picks the
  tool. Best for almost everything.
- **By-name invocation** (this article): *"Use the Grep tool with `pattern`='validateAuth',
  `output_mode`='content'."* You pick the tool and, optionally, its exact arguments. Best
  when the specific behavior matters more than the outcome.

Switch to by-name when any of these is true: you want a **specific mechanism** (this tool,
not a substitute), you want a behavior the model **wouldn't trigger on its own**, or the
tool is **keyword-gated** and has no other way in.

## When naming the tool wins

### Make it stop and ask you: `AskUserQuestion`

Here is a fun one to try right now. Prompt the model with:

> "Use the AskUserQuestion tool and present me three color choices."

It pops a little structured menu, red / green / blue, and waits for you to pick. Nothing
about choosing a color *needs* a tool, which is exactly why it makes such a clean demo: you
aren't asking for an outcome, you are commandeering a specific piece of UI on demand. The
model is smart enough to reach for this tool on its own, but here you are the one deciding
it should.

Now swap the toy for something real:

> "Use the AskUserQuestion tool to give me options before you choose the auth library."

Same move, real stakes. You convert "the model decided" into "the model asked." This is the
single most useful by-name habit to build.

### Force a task to exist: `TaskCreate` / `TaskUpdate`

The todo system auto-engages on big, multi-step work, but for a two- or three-step job it
may not bother. When you want a durable, visible checklist regardless of the model's
judgment:

> "Use **TaskCreate** to add a task: `subject`='Migrate auth to OAuth'. Then keep it updated
> as you go."
> "Use **TaskUpdate** `taskId`=2, `addBlockedBy`=['1'] so task 2 waits for task 1."

Naming `TaskCreate` makes the task exist on purpose; naming `TaskUpdate` lets you drive
status and dependencies by hand instead of hoping they're tracked.

### Start something recurring or watching, on purpose: `CronCreate`, `Monitor`, `/loop`

"Keep an eye on the build" is ambiguous; the mechanism you actually want isn't. Name it, and
you choose polling vs. streaming vs. self-pacing:

> "Use **CronCreate** `cron`='*/15 * * * *' to re-run the tests every 15 minutes."
> "Use **Monitor** `command`='tail -f dev.log | grep --line-buffered ERROR' to tell me every
> time an error appears."
> "**/loop 5m /babysit-prs**" to run a prompt on an interval.

### Behaviors that *only* exist by name: the keyword-gated tools

Some tools cannot be summoned any other way. Here the name or keyword **is** the API:

- **worktree** unlocks `EnterWorktree`: "do this in a **worktree**."
- **ultracode** (or "use a workflow") unlocks `Workflow`: deliberate multi-agent orchestration.
- **/loop** with no interval unlocks `ScheduleWakeup`: model-paced loops.
- an active code review unlocks `ReportFindings`: typed findings to the UI.

No amount of outcome phrasing unlocks these; the deliberate keyword is the point. They can
spawn many agents or relocate your edits, so they wait for explicit consent.

### Pin the exact operation: `Grep`, `Glob`, `Edit`, `Agent`

When you want one specific mechanism and no substitution:

> "Use **Grep** with `pattern`='TODO', `output_mode`='content', `-n`=true" (not a subagent).
> "Use an **Explore** agent to search, don't grep inline."
> "Use **Edit** on `config.ts` with `replace_all`=true."

Naming removes the routing guess when the *how* matters as much as the *what*.

### Run a specific, known procedure: `Skill`

Skills are packaged multi-step procedures. Naming one runs *that* procedure rather than
hoping the model reconstructs the same steps:

> "Use the **deep-research** skill on X", or type `/deep-research <question>`.

### Choose the output channel: `Artifact`, `PushNotification`, `WebFetch`

When you want a specific destination for the result:

> "Make an **Artifact** I can share." / "Use **PushNotification** to ping me when the build
> finishes." / "Use **WebFetch** `url`=… `prompt`='what changed in 2.1?'."

### How to phrase a by-name call

Two levels of precision:

- **Name only:** "Use TaskCreate to track this." The model fills in the arguments.
- **Name plus arguments:** "Use TaskCreate with `subject`='…', `description`='…',
  `activeForm`='…'." You pin the exact call.

The rest of this article is the reference that makes the second form possible: every tool,
its parameters, types, and defaults.

---

## The catalog: always-loaded vs deferred

The reference is the full toolset of a Claude Code 2.1.223 session (Windows, August 2026):
**32 tools, 13 always-loaded and 19 deferred**. Always-loaded tools are callable
immediately; deferred tools are announced by name and their schemas load on demand (the
model pulls one in with a `ToolSearch` call before first use). Parameters are marked `*`
when required, with types and defaults noted.

### File, search & shell

- **Read**: read a file (text, image, PDF, notebook).
    - `file_path`* string (absolute) · `offset` int · `limit` int · `pages` string (PDF range)
- **Write**: create or overwrite a file you've Read.
    - `file_path`* string · `content`* string
- **Edit**: exact string replacement.
    - `file_path`* string · `old_string`* string · `new_string`* string · `replace_all` bool=false
- **Glob**: filename pattern match, sorted by mtime.
    - `pattern`* string · `path` string
- **Grep**: ripgrep content search.
    - `pattern`* string · `path` · `glob` · `type` · `output_mode` content|files_with_matches|count · `-i`/`-n`/`-o` bool · `-A`/`-B`/`-C` int · `multiline` bool · `head_limit` int=250 · `offset` int
- **Bash**: Git Bash / POSIX shell.
    - `command`* string · `description` · `timeout` ms=120000 (max 600000) · `run_in_background` bool · `dangerouslyDisableSandbox` bool
- **PowerShell**: pwsh (primary shell on Windows; absent on macOS/Linux).
    - `command`* string · `description` · `timeout` ms (max 600000) · `run_in_background` bool · `dangerouslyDisableSandbox` bool
- **NotebookEdit**: replace/insert/delete one Jupyter cell.
    - `notebook_path`* string · `new_source`* string · `cell_id` string · `cell_type` code|markdown · `edit_mode` replace|insert|delete (default replace)

### Delegation

- **Agent**: spawn a subagent.
    - `description`* string (3 to 5 words) · `prompt`* string · `subagent_type` string · `model` sonnet|opus|haiku|fable · `isolation` worktree|remote · `run_in_background` bool=true
    - `subagent_type` values: `claude`, `general-purpose`, `Explore`, `Plan`, `statusline-setup`.
- **SendMessage**: the only way agents talk to each other; resume a spawned agent with its context, or post to `main`.
    - `to`* string (teammate name, or `"main"`) · `message`* string · `summary` string (<=200, required when message is a string)
- **Workflow** *(gated: ultracode)*: deterministic JS orchestration over many subagents.
    - `script` string · `scriptPath` string · `name` string · `args` any · `resumeFromRunId` (`wf_…`)

### Task list & background jobs

- **TaskCreate**: new task (starts `pending`).
    - `subject`* string · `description`* string · `activeForm` string · `metadata` object
- **TaskUpdate**: modify a task.
    - `taskId`* string · `status` pending|in_progress|completed|deleted · `addBlockedBy[]` · `addBlocks[]` · `owner` · `subject` · `description` · `activeForm` · `metadata`
- **TaskList**: list all tasks. *(no params)*
- **TaskGet**: one task's full detail.
    - `taskId`* string
- **TaskStop**: kill a background task or named agent.
    - `task_id` string (also `name@team` or a bare agent name) · `shell_id` *(deprecated)*
- **TaskOutput** *(deprecated)*: read a background job's output.
    - `task_id`* string · `block` bool=true · `timeout` ms=30000 (max 600000)

### Scheduling & watching

- **CronCreate**: schedule a prompt on 5-field local-time cron; session-only, recurring auto-expires after 7 days.
    - `cron`* string · `prompt`* string · `recurring` bool=true · `durable` (no effect)
- **CronList**: list this session's cron jobs. *(no params)*
- **CronDelete**: cancel one. · `id`* string
- **Monitor**: stream events from a command's stdout or a WebSocket; one notification per line.
    - `description`* string · `timeout_ms`* number=300000 (max 3600000) · `persistent`* bool=false · `command` string **or** `ws` {`url`* string, `protocols[]`} (mutually exclusive)
- **RemoteTrigger**: claude.ai cloud routines (backs `/schedule`).
    - `action`* list|get|create|update|run · `trigger_id` string · `body` object (required for create/update)
- **ScheduleWakeup** *(gated: /loop without interval)*: self-paced loop scheduling.
    - `delaySeconds` number (60 to 3600) · `prompt` string · `reason` string · `stop` bool

### Web & output

- **WebFetch**: fetch a URL, convert to markdown, answer a prompt (15-min cache, HTTP upgraded to HTTPS, no auth).
    - `url`* uri · `prompt`* string
- **WebSearch**: web search (US-only).
    - `query`* string (min 2) · `allowed_domains[]` · `blocked_domains[]`
- **PushNotification**: desktop/phone ping.
    - `message`* string (<200 chars) · `status`* const `"proactive"`
- **AskUserQuestion**: structured multiple-choice back to you (interactive only).
    - `questions[]`* where each has: `question` string, `header` string (<=12), `options[]` (2 to 4: `label`, `description`, `preview`), `multiSelect` bool

### Planning, isolation & design

- **EnterPlanMode / ExitPlanMode**: read-only planning and approval (interactive; also shift+tab). *(no params)*
- **EnterWorktree** *(gated: worktree)*: isolated git worktree.
    - `name` string (<=64) **or** `path` string, mutually exclusive
- **ExitWorktree**: leave the worktree session.
    - `action`* keep|remove · `discard_changes` bool (true to remove a dirty worktree)
- **DesignSync**: read/write claude.ai design-system projects; ordering is read, then `finalize_plan`, then write.
    - `method`* list_projects|get_project|list_files|get_file|finalize_plan|write_files|delete_files|register_assets|unregister_assets|create_project|report_validate · plus `projectId`, `planId`, `path`/`paths[]`, `writes[]`/`deletes[]`, `localDir`, `files[]`, `assets[]`, `name`

### Specialized & internal

- **Skill**: run a packaged skill / slash command.
    - `skill`* string (exact name, no leading slash) · `args` string
- **ReportFindings** *(gated: code-review)*: typed findings to the UI.
    - `findings[]`* where each has: `file`*, `summary`*, `failure_scenario`*, `line`, `category`, `short_summary`, `verdict`, `outcome` · `level` low|medium|high|xhigh|max
- **Artifact**: publish an HTML/Markdown file as a private claude.ai page (interactive).
    - `file_path` string · `favicon` emoji (required to publish) · `title` · `description` · `url` (update) · `action` publish|list
- **ToolSearch**: load deferred tool schemas on demand (internal; never prompt for it).
    - `query`* string (`select:A,B`, keywords, or `+name term`) · `max_results`* number=5

## Compact reference

Loaded = always-on; Deferred = schema loads on demand. `*` = required.

| Tool | Loaded | Purpose | Key arguments |
|------|--------|---------|---------------|
| Read | always | read file / image / PDF / notebook | `file_path`*, offset, limit, pages |
| Write | always | create / overwrite a Read file | `file_path`*, `content`* |
| Edit | always | exact string replacement | `file_path`*, `old_string`*, `new_string`*, replace_all |
| Glob | always | filename pattern match | `pattern`*, path |
| Grep | always | ripgrep content search | `pattern`*, path, glob, type, output_mode, -i/-n/-o, -A/-B/-C, multiline, head_limit |
| Bash | always | POSIX shell | `command`*, timeout, run_in_background |
| PowerShell | always (Windows) | pwsh shell | `command`*, timeout, run_in_background |
| NotebookEdit | deferred | edit one Jupyter cell | `notebook_path`*, `new_source`*, cell_id, cell_type, edit_mode |
| Agent | always | spawn a subagent | `description`*, `prompt`*, subagent_type, model, isolation, run_in_background |
| SendMessage | deferred | agent-to-agent message | `to`*, `message`*, summary |
| Workflow | always (gated: **ultracode**) | multi-agent orchestration script | script / scriptPath / name, args |
| TaskCreate/List/Get/Update | deferred | todo tracking and dependencies | `subject`/`description`*; `taskId`*; status, addBlockedBy/addBlocks |
| TaskStop | deferred | kill background task / agent | task_id |
| TaskOutput | deferred (**deprecated**) | read a background job's output | `task_id`*, block, timeout |
| CronCreate/List/Delete | deferred | in-session scheduled prompts | `cron`*, `prompt`*, recurring; `id`* |
| Monitor | deferred | stream events from logs / cmds / WebSockets | `description`*, `timeout_ms`*, `persistent`*, command \| ws |
| RemoteTrigger | deferred | cloud routines (`/schedule`) | `action`*, trigger_id, body |
| ScheduleWakeup | always (gated: `/loop`) | self-paced loops | delaySeconds, prompt, reason, stop |
| WebFetch / WebSearch | deferred | fetch a URL / web search | `url`*+`prompt`*; `query`* |
| PushNotification | deferred | desktop / phone ping | `message`*, `status`* |
| AskUserQuestion | interactive | structured question to the user | `questions[]`* |
| EnterPlanMode / ExitPlanMode | interactive | read-only planning and approval | *(none)* |
| EnterWorktree / ExitWorktree | deferred (gated: **worktree**) | isolated git worktree | name \| path; `action`*, discard_changes |
| DesignSync | deferred | claude.ai design-system read/write | `method`*, projectId, planId, writes/deletes, files, assets |
| Skill | always | run a slash command | `skill`*, args |
| ReportFindings | always (gated: code-review) | typed findings to UI | `findings[]`*, level |
| Artifact | interactive | publish HTML/MD as a private page | file_path, favicon, url, action |
| ToolSearch | always | load deferred tool schemas | `query`*, `max_results`* |

---

## How this was measured

Three methods, each more authoritative, and no network capture needed. The parameter
schemas come straight from the running session.

- **Ask, interactively:** "list all your tools with their arguments."
- **Headless, scriptable:** `claude -p "Introspect and list every tool… output markdown."`
  One thing to know: headless `-p` exposes a reduced toolset. Artifact, AskUserQuestion, and
  the plan-mode tools don't appear (no interactive surface), so add them back by hand.
- **Read the session logs:** `~/.claude/projects/<encoded-cwd>/<session-uuid>.jsonl` records
  the roster (`tool_reference`, `deferred_tools_delta`, `skill_listing`,
  `agent_listing_delta`) from the harness, independent of the model, though not the argument
  schemas (`inputSchema` isn't in the transcript).

The typed parameters above come from `ToolSearch` loading each deferred tool's full
JSONSchema, the same definition the CLI would send on the wire, so a packet capture would
only show what the session already hands over.

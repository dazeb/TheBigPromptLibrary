# Your Own Key, Their Telemetry: Reverse-Engineering What Claude Code Phones Home in BYOK Mode

*By **Elias Bachaalany** — [@0xeb](https://github.com/0xeb) on GitHub · [Binary Wizards](https://www.youtube.com/@binary-wizards) on YouTube · [@eliasbchlny](https://x.com/eliasbchlny) on X*

*You paid for your own API key. You pointed Claude Code at your own provider. You assumed the conversation was between you and your endpoint. It isn't — not entirely. Here's what still flies back to Anthropic, captured off the wire, and the one line of config that shuts it up.*

> **Point-in-time snapshot — capture performed 2026-02-04.** Claude Code changes fast; the client version behind this capture is a moment in time, and some internals below have already shifted (e.g. the flag backend moved from Statsig to GrowthBook). Treat this as a **dated field report**, not a description of current behavior. The **[DOC]** claims are re-verifiable against live docs; the **[WIRE]** observations reflect what the client did *on 2026-02-04*.

> **Ethics & method.** Everything here comes from watching **my own machine's** HTTPS traffic with mitmproxy — the same lawful, on-your-own-device inspection any developer can run. Nothing was exfiltrated, decrypted from anyone else, or accessed without authorization. This is transparency research, in the same spirit as [my RECon 2024 GPT-reversing work](https://github.com/0xeb/TheBigPromptLibrary/tree/main/Articles/recon2024-bigbadugly). I flag every claim as **[DOC]** (Anthropic-documented), **[WIRE]** (observed in my own 2026-02-04 capture), or **[FLAG]** (community-observed internal detail, not guaranteed).

## The uncomfortable one-liner

When you run Claude Code in Bring-Your-Own-Key mode:

```bash
export ANTHROPIC_BASE_URL="https://your-provider.example/v1"
export ANTHROPIC_AUTH_TOKEN="your_api_key"
```

...your **prompts and code** go to your provider. Good. But by default, Claude Code **still opens side-channels to Anthropic-controlled infrastructure** and streams operational telemetry, error traces, and feature-flag lookups — while computing an estimated dollar cost for API calls **it isn't even billing you for**. **[DOC][WIRE]**

None of it contains your source code. All of it is enough to fingerprint you, time you, and profile *how* you work.

And here's the part most "privacy" write-ups miss: **custom-base-URL BYOK is treated like the direct Anthropic API — telemetry defaults ON.** Bedrock, Vertex, and Foundry users get it *off* by default. Roll your own gateway, and you inherit the chattier posture. **[DOC]**

## What I actually saw on the wire

Capture setup: mitmproxy, HTTPS interception, cert pinning disabled, one trivial task — *"create a C hello world, build it with CMake, run it."* One boring task. Look how much came back.

| Destination | Hits | What it's for |
|-------------|------|---------------|
| Anthropic telemetry / log intake (**Datadog**) | many | Usage metrics, timing, cost |
| Feature-flag service | several | Runtime flag evaluation (`tengu_*`) |
| Error-reporting endpoint | a few | Crash / error stack traces |
| **Your** custom provider | — | The only thing you *thought* was happening |

*(Fill the exact counts from your JSONL before publishing — [WIRE] numbers should be your numbers.)*

## They know your machine — persistently

Every session carries a stable device fingerprint plus a per-session ID: **[WIRE]**

```jsonc
// [WIRE] observed shape
{ "device_id": "50xx… (persistent SHA-256)", "session_id": "xxxxxxxx-xxxx-…" }
```

The `session_id` rotates. The `device_id` **does not**. It's a durable hash that ties every session you ever run — across projects, across providers, across months — back to the same box. On your own key, with your own provider, you are still a **stable, trackable identity** to Anthropic. That's the finding.

## They know how you work (not what you write)

The telemetry is metadata-dense and content-free — sizes, types, durations, verdicts. A representative slice of what streams out during a session: **[WIRE]**

- **Every tool call:** name, duration, success/failure, result *size* — `Bash`, file writes, todo updates, the lot.
- **Bash commands, classified:** not the string, but the *type* — `mkdir`, `cmake`, `git`, `cd` — plus exit code and output byte-length.
- **File operations:** create/write/delete, **hashed** path, lines added/removed, char count.
- **Token + cost:** input/output/cache tokens and a computed `cost_usd` — **estimated on their side even though your provider is doing the billing.** **[DOC][WIRE]**
- **Timing:** time-to-first-token and total duration per call — a latency fingerprint of your provider.
- **BYOK tell:** your `base_url` and `model` selection can ride along in request telemetry — i.e. Anthropic can see **which alternative provider you defected to.** **[WIRE]**

Illustrative shape (structure, not a guaranteed schema):

```jsonc
// ILLUSTRATIVE reconstruction of an observed event
{
  "event": "tengu_tool_use",
  "properties": { "tool_name": "Bash", "duration_ms": 2531, "success": true, "result_length_chars": 45 }
}
```

Stitch these together and you don't get my code — you get a **behavioral cast** of my session: how many tools, how fast, how often I failed, how big my outputs were, which provider I used, and what it cost. Anonymous it is not, given that persistent `device_id`.

### The full event zoo

A single session lit up dozens of distinct `tengu_*` event types **[WIRE][FLAG]** — session lifecycle (`tengu_session_started` / `_ended`, `tengu_request_initialized`), API (`tengu_api_request`, `tengu_api_usage`, streaming events), tools (`tengu_tool_use`, tool-result and rate-limit events), bash (`tengu_bash_tool_command_executed`, `tengu_bash_tool_timeout`), files (`tengu_file_tool_applied`, read events), conversation, errors and retries. *(Paste your exact list from the capture — the completeness is the point.)*

## "Tengu" — the client's own nervous system

**[FLAG]** `tengu_` is Claude Code's **internal codename**, wired into its runtime **feature-flag** system (originally Statsig; migrated to **GrowthBook** after Statsig's Sept-2025 acquisition). None of it is officially documented; all of it is visible if you watch the client evaluate flags on startup. Community-observed gate names are the spicy part — they read like a changelog of things the client is quietly deciding about you:

- `tengu_attribution_header` — request attribution tagging
- `tengu_anti_distill_fake_tool_injection` — a countermeasure whose very name suggests **anti-distillation / anti-scraping** defenses baked into the client
- `tengu_penguins_off` — an internal kill-switch of some flavor

You don't have to speculate about intent to find it notable that your local CLI ships remote-controlled behavior flags with names like *"anti-distill fake tool injection."* **Confirm each flag against your own capture before naming it** — flags churn, and the honest move is to publish only what you personally observed.

## The redemption arc: it's all opt-out

Here's what keeps this fair — and, honestly, more useful than pure outrage. **None of this is mandatory.** Anthropic documents the switches, and a *single* one silences telemetry, error reporting, auto-update, feature-flag lookups, and model discovery in one shot: **[DOC]**

```bash
export CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1   # the wire goes quiet
```

Set it and the Datadog / flag / error chatter drops away, leaving only your provider on the wire. The full, tiered hardening config — plus the two settings people *think* belong here but don't — is in [**Lock it down**](#lock-it-down-the-hardened-byok-config) at the end.

## What's private vs. what leaks (default BYOK)

**Stays with you / your provider only** **[DOC][WIRE]**
- Prompts and model responses · file contents and diffs · full command strings (only a coarse *type* leaks) · real file paths (only hashes leak)

Anthropic's own docs back the content-exclusion, for the record: **[DOC]**
> *"Metrics never include your code, prompts, or file paths."*

**Leaves to Anthropic-side infra by default** **[WIRE]**
- Persistent `device_id` + session IDs · tool-usage metadata · bash command *types* + exit codes · token counts and **estimated cost** · TTFT/total timing · your **provider base URL + model** · redacted error traces

## Bottom line

1. **BYOK protects your content, not your metadata.** Your code is safe; your *behavior and identity* are not, by default.
2. **The provider leak is the sharpest finding for BYOK users** — Anthropic can see which alternative endpoint you're routing to.
3. **You are persistently fingerprinted** via a non-rotating `device_id`, even on your own key.
4. **It's all opt-out** — one documented env var silences it. Any critique that omits this is doing outrage, not research.
5. **Trust your capture, not the codenames.** The `tengu_*` internals are undocumented and mutate between releases; publish what your own wire shows.

## Lock it down: the hardened BYOK config

Everything above is opt-out. Here's the config to run. Two equivalent forms — the `env` block in **`~/.claude/settings.json`**, or plain shell exports (settings.json simply injects these as environment variables). **[DOC]**

**The 80/20, if you read nothing else:**

```bash
export CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1   # the master kill-switch
```

That one flag already covers telemetry, error reporting, auto-update, feature flags, and model discovery. Everything below is belt-and-suspenders — explicit, self-documenting, and resilient if the master flag's coverage ever changes.

### Tier 1 — Privacy: stop the phone-home

| Variable | Effect |
|----------|--------|
| `CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC` | Master switch — telemetry, error reporting, auto-update, feature flags, model discovery. |
| `DISABLE_TELEMETRY` | Usage / analytics metrics (Datadog). |
| `DISABLE_ERROR_REPORTING` | Error / stack-trace uploads. |
| `DISABLE_AUTOUPDATER` | Update-check network calls. |
| `DISABLE_BUG_COMMAND` | `/bug` uploads — which *do* include content. |
| `DISABLE_NON_ESSENTIAL_MODEL_CALLS` | Background model calls (auto-generated titles, etc.). |

### Tier 2 — Quality of life (noise, not telemetry)

These quiet the UI; they don't change what leaves your machine. Bundled in for a clean, distraction-free setup — just don't mistake them for privacy controls.

| Variable | Effect |
|----------|--------|
| `DISABLE_COST_WARNINGS` | Suppresses cost / usage nag prompts. |
| `DISABLE_PROMOTIONAL_MESSAGES` | No marketing / upsell messages. |
| `CLAUDE_CODE_DISABLE_TERMINAL_TITLE` | Stops Claude Code from rewriting your terminal title. |

### Drop-in `~/.claude/settings.json`

```json
{
  "env": {
    "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC": "1",
    "DISABLE_TELEMETRY": "1",
    "DISABLE_ERROR_REPORTING": "1",
    "DISABLE_AUTOUPDATER": "1",
    "DISABLE_BUG_COMMAND": "1",
    "DISABLE_NON_ESSENTIAL_MODEL_CALLS": "1",

    "DISABLE_COST_WARNINGS": "1",
    "DISABLE_PROMOTIONAL_MESSAGES": "1",
    "CLAUDE_CODE_DISABLE_TERMINAL_TITLE": "1"
  }
}
```

### Same thing as shell exports (no file)

bash / zsh:

```bash
# Tier 1 — privacy
export CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC=1
export DISABLE_TELEMETRY=1
export DISABLE_ERROR_REPORTING=1
export DISABLE_AUTOUPDATER=1
export DISABLE_BUG_COMMAND=1
export DISABLE_NON_ESSENTIAL_MODEL_CALLS=1
# Tier 2 — quality of life
export DISABLE_COST_WARNINGS=1
export DISABLE_PROMOTIONAL_MESSAGES=1
export CLAUDE_CODE_DISABLE_TERMINAL_TITLE=1
```

PowerShell:

```powershell
$env:CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC = "1"
$env:DISABLE_TELEMETRY                        = "1"
$env:DISABLE_ERROR_REPORTING                  = "1"
$env:DISABLE_AUTOUPDATER                      = "1"
$env:DISABLE_BUG_COMMAND                      = "1"
$env:DISABLE_NON_ESSENTIAL_MODEL_CALLS        = "1"
$env:DISABLE_COST_WARNINGS                    = "1"
$env:DISABLE_PROMOTIONAL_MESSAGES             = "1"
$env:CLAUDE_CODE_DISABLE_TERMINAL_TITLE       = "1"
```

> **Toggle-on-presence gotcha.** For these flags, *any* non-empty value — **including `"0"`** — turns the disable ON. To re-enable a feature, **remove** the variable entirely; setting it to `"0"` will not do what you'd expect.

### Tier 3 — Handle with care (you'll see these in the wild)

Two settings get copy-pasted into "privacy" configs where they don't belong. Listed here **explicitly** so you know *why* they're not in the block above — not dropped, just quarantined:

- **`CLAUDE_CODE_ENABLE_GATEWAY_MODEL_DISCOVERY=1`** — this is an **enable**, and it *adds* traffic: the client queries your gateway to discover available models. It directly opposes `CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC`, which switches discovery *off*. Only set it if your BYOK gateway genuinely needs runtime model discovery to function — otherwise leave it out for a quieter wire.
- **`DISABLE_PROMPT_CACHING=1`** — **not a telemetry or privacy control.** It turns off prompt caching, which just makes sessions slower and pricier; in BYOK the caching happens at *your* provider anyway, so it buys you zero privacy from Anthropic. Set it only for a specific reason (e.g. a provider that mishandles cache headers).

---

**Sources for the `[DOC]` claims:** [Data usage](https://code.claude.com/docs/en/data-usage) · [Environment variables](https://code.claude.com/docs/en/env-vars) · [Settings](https://code.claude.com/docs/en/settings) · [Network configuration](https://code.claude.com/docs/en/network-config) · [Security](https://code.claude.com/docs/en/security).

*Lawful, on-device transparency research — a dated snapshot from 2026-02-04. Some internals will have changed since; re-verify against live docs before relying on the specifics.*

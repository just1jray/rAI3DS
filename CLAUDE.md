# rAI3DS – Agent context

## What this project is

**rAI3DS** is a Nintendo 3DS companion app for AI coding agents (Claude Code, etc.).

## Reference projects (architecture & orchestration)

Use these when designing or evolving **architecture** (agent orchestration, hooks, companion bridge, multi-agent UI):

| Project | Notes |
|--------|--------|
| [clawdgotchi](https://github.com/stevysmith/clawdgotchi) | Open source, good reference for agent orchestration |
| [vibecraft](https://github.com/Nearcyan/vibecraft) | Open source, good reference for functional architecture |
| [ralv.ai](https://ralv.ai/) | Inspirational only; not open source |

## Design inspiration (UX & agent control)

- **Pokémon-style menuing and turn-based battles** – Use as inspiration for:
  - **Functionality**: clear approve/deny flows, state transitions, feedback, menu nesting
  - **Agent control**: turn-taking, waiting for user input, status display

## Conventions

- Save all plans to the **plans** directory

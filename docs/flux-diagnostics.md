# Flux logging

Flux uses the logger named `flux`. Each record has a level, a message, and its
source location.

## Levels

- `trace` records detailed execution data.
- `debug` records backend setup and internal decisions.
- `info` records renderer configuration, progress, and results.
- `warning` reports a recoverable problem and any selected fallback.
- `error` reports a failed operation.

## When to log

Log events that help users and developers understand a render: the selected
device, backend build summary, fallback, and render result. For a long
operation, log its start and completion when the completion includes timing,
counts, or an output path.

Let errors propagate to the application or host, which logs each final error
once. Cleanup code and external callbacks log errors they cannot propagate.

## Messages

- Use simplified English.
- Start library messages with the responsible type or external system, such as
  `TriangleMesh: ...` or `OptiX: ...`.
- Quote paths and authored values with single quotes.
- State any selected fallback in a Flux-authored warning.
- Keep each record on one line. Compiler and driver output may keep its original
  line breaks.
- End without a period.

## Hosts

The CLI writes logs to stderr and the render summary to stdout. It shows
warnings and errors by default. `--log-level` selects another level, and
`--log-file` appends the same records to a file.

Kira formats CLI records as:

```text
[<time>] [<module>] [<level>] <message>
```

Debug builds also include the source file and line. Levels use `T`, `D`, `I`,
`W`, and `E`.

The Hydra integration registers its logger before creating renderer objects.
It maps warning and error records to Hydra diagnostics and sends trace, debug,
and info records to the host log. Hydra controls its console output.

## Threads

Flux may log from any host thread, so a host logger must support concurrent
calls. The CLI logger is thread-safe. Register the logger before renderer
threads start. Backends log GPU errors reported by a host API call or callback.

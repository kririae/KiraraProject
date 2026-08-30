# FLux Development Rules

## Input handling

- Validate scene input with an exception only when continuing could access invalid memory.
- Propagate failures from required system APIs.
- Bad scene values are not exceptional. Bound them to the supported domain and keep rendering.
- Do not add defensive NaN or infinity checks to rendering code. Let arithmetic propagate unless it can make memory access unsafe.

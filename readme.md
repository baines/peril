Very hacky "C in lisp" toy language.

Hastily written for the Handmade Network 2020 lisp jam.

- Evaluating an unknown symbol loads it via dlsym and calls it.
- There is no type / count checking of arguments.
- It doesn't support a large swath of stuff, e.g.
    - floating point
    - funcs with more arguments than fit in registers (passed on stack)
    - will only run on x86\_64 linux
- There are not enough builtin functions, I only wrote the ones needed for the demo program.
- Probably loads of bugs

`./peril < demo.txt`

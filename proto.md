#Blaze prototyping

`*T` - Raw, unmanaged pointer.
`&T` - Unique pointer.
`@T` - Smart pointer.
`%T` - Atomic smart pointer.

All can be mutable, i.e. `*mut T`. This is the ONLY time mutability is part of the
type!

Must have function-level ownership (not global).

Like:

```python
x: &mut T

fun def(y: &mut T):
    # Y is marked as "escapable".
    x = y

fun xyz():
    let x: &mut T
    *x = 123
    def(x) # ERROR!
```

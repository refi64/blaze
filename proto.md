#Blaze prototyping

##Pointers

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

##Removing new and delete

Changing this:

```python
struct X:
    new: return
    delete: return
```

to:

```python
struct X:
    fun __init__: return
    fun __del__: return
```

Also, everything should be mutable in the constructor.

##Moving + returning == explosion (fixed)

Take a look at this:

```python
struct X:
    x: *byte
    new:
        @x = malloc(1)
    delete:
        free(@x)

fun f(x: X) -> X: return x

fun main -> int:
    let x = new X
    let y = f(x)
```

At the end of `main`, `X.x` will be deleted *twice*. Functions take arguments by
reference, so `f` returns the original `main:x`. This itself is an issue, because
`x` is immutable, but `f` is moving the value. When `main` ends, destructors will
end up being called on both `main:x` and `main:y`.

However, this could still cause trouble with mutable arguments. Maybe there should
be an argument annotation that requires its input argument to be moved. But then
I'd need control flow analysis...

An option in the meantime might be to make it so that a function must "own"
anything that is returned, maybe via `dup`. The problem would be that, when
indexing is introduced, this could happen:

```python
struct Y:
    values: *X
    # Constructors, destructors, etc.
    fun __index__[T<:Integral](i: int) -> X:
        return @values[i]
```

`__index__` will move the item out of `@values`. Not good!

An easy fix could also be to add a simple rule for moving: move constructors are
only called if the returned value is non-addressable or is a local variable.

Note that this could cause issues with indexing a local:

```python
fun f:
    let mem = malloc(sizeof(X)) :: *X # Yeah, this doesn't actually work ATM...
    return mem[0]
```

Should `mem[0] :: X` be moved or not? It depends. To be safe, it would be best to
just say it should be copied, since it won't really hurt anything.

##Variable ids

This is an ugly hack. Take this code:

```python
struct Y:
    new: return
    fun f -> int: return 0

fun main -> int:
    return new Y.f()
```

If I add a `struct X:\n new: return` to the beginning, the whole thing will be
recompiled, since it'll throw the type id system off. Grrr...

##Destructors

This doesn't work:

```python
fun f:
    if myval:
        return
    let z = "abc"
    return
```

The first `return` will end up destroying `z`...which hasn't been created yet!

A possible solution could be to generate the destructors for "active" variables
on the spot; for instance:

```python
fun f:
    let a = "abc"
    if myval:
        return # Here, a is destroyed.
    let z = "abc"
    return # Here, z is destroyed.
```

In addition, currently, assignments don't call destructors:

```python
fun f:
    let var a = "abc"
    a = "def" # a's destructor should be called.
```

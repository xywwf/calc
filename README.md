By example
===

Values
---
    ≈≈> 12
    12
    ≈≈> 3.1415
    3.1415
    ≈≈> "Hello, world"
    Hello, world
    ≈≈> "こんにちは世界"
    こんにちは世界

Operators
---

- “Set-tish”:

  `==`, `!=`

- “Magmatic”:

  `+`

- “Group-ish”:

  `-` (both unary and binary)

- “Ring-ish”:

  `*`

- “Euclidean ring-ish”:

  `%`

- “Field-ic”:

  `/`

- “Real-ish”:

  `^`

- “String-ish”:

  `~~` (concatenation)

- “Order-al”:

  `<`, `>`, `<=`, `>=`

- “Logical”:

  `&&`, `||`, `!`

Expressions & grouping
---
    ≈≈> 2+2*2
    6
    ≈≈> (2+2)*2
    8
    ≈≈> 2+2; acos(-1) == Pi
    4
    1

Matrices
---
    ≈≈> []
    [
    ]
    ≈≈> [1, 1+1, 1+1+1, 1+1+1+1]
    [
        1   2   3   4
    ]
    ≈≈> [1,2; 3,4]
    [
        1   2
        3   4
    ]
    ≈≈> [1,2,3; 4,5,6]
    [
        1   2   3
        4   5   6
    ]
    ≈≈> [1; 2, 3; 4]
    > [1; 2, 3; 4]
              ^ wrong row length

Variables
---
    ≈≈> a = 2
    ≈≈> a
    2

Indexing
---
    ≈≈> a = [1,2; 3,4; 5,6]
    ≈≈> a
    [
        1   2
        3   4
        5   6
    ]
    ≈≈> a[1]; a[2]; a[6]
    1
    2
    6
    ≈≈> a[1,1]; a[2,2]; a[3,2]
    1
    4
    6
    ≈≈> a[3,1] = 0
    ≈≈> a
    [
        1   2
        3   4
        0   6
    ]
    ≈≈> a[5] = 5
    ≈≈> a
    [
        1   2
        3   4
        5   6
    ]

Conditions
---
    if 2+2 == 2 then
        "Yay symmetry"
    elif 2+2 == 3 then
        "It’s three"
    else
        "It’s " ~~ 2+2
    end

    # Output:
    # It’s 4

Loops
---

    x = round(Rand() * 100)
    while 1 do
        n = Input()
        if n < x then
            "Too low"
        elif n > x then
            "Too high"
        else
            break
        end
    end

Or, using `for`,

    x = round(Rand() * 100)
    for n | Input(); n != x; Input() do
        if n < x then
            "Too low"
        else
            "Too high"
        end
    end

`next` instruction (== `continue` in some other languages) is also supported.

Functions & local variables
---
    fu double(x)
        return x * 2
    end
    double([10,20,30])

    # Output:
    # [
    #     20  40  60
    # ]

    fu powsum(n,p)
        r := 0
        for i | 1; i<=n; i+1 do
            r = r + i^p
        end
        return r
    end
    n = 102
    p = -9
    r = 3.3
    powsum(5,6)
    r; n; p

    # Output:
    # 20515
    # 3.3
    # 102
    # -9

Built-in functions
---

  * `sin`
  * `cos`
  * `tan`
  * `asin`
  * `acos`
  * `atan`
  * `ln`
  * `exp`
  * `trunc`
  * `floor`
  * `ceil`
  * `round`
  * `Mat(n,m)` returns a new zero-filled `n`-by-`m` matrix
  * `Dim(M)` returns dimensions of a matrix as `[height, width]`
  * `Trans(M)` returns the transposition of a matrix
  * `DisAsm(f)` disassembles a user-defined function
  * `Kind(v)` returns the type name of `v` as a string
  * `Rand()` returns a random number in `[0, 1)`
  * `Input()` reads a number from stdin

Built-in constants
---

  * `Pi`
  * `E`

Caveats
===

- There’s no specialized memory allocator. Link with jemalloc or something if you have to.

# The Hagane Language Guide (version 0.1.1)

Welcome! This document will explain how to use the Hagane programming language.

This guide assumes basic knowledge of programming. It is also useful, although not necessary, to be familiar with functional programming.

Note that the language is still in an early stage of development, so many things described here will change in the future.

## Compiling

First, install the Hagane compiler using the instructions in `README.md`.

To compile your first Hagane program, write the following to `program.hgn`:
```
print_int(1)
```
and execute:
```
hagane-0.1 program.hgn -oprogram
```

The resulting program will be saved to a file named `program`. When executed, it will print the number `1`.

All other programs can be compiled this way.

## Variables

Variables in Hagane are represented using identifiers (variable names).
Variable names in Hagane may use a considerably wider range of characters than in most programming languages.

An identifier may consist of characters with Unicode General Category L\* (Letter), M\* (Mark), N\* (Number), Pc (Punctuation, Connector), Pd (Punctuation, Dash), or S\* (Symbol).
The following characters from category Po (Punctuation, Other) are also permitted: `%&*/\؉؊٪‰‱′″‴‵‶‷⁂⁗⁎⁑⁕﹠﹡﹨﹪％＆＊／＼`.

However, an identifier may not begin with a numeric character (with General Category N\*) or consist of a single undescore.

For example, the following are all valid identifiers:
```
index
max_value
if
_x
+
I/O
%100
∃
∈ℝ
名前
```
while these are not:
```
hey!
50%
_
if.
```

Variables may be declared using a `let.` statement.
```
let. x 1
print_int(x)
```
This program, like the previous, prints `1`.
`let. x 1` creates a variable named `x` with value `1`.

Hagane keywords are distinguished from regular identifiers by ending with a period. That's why `let.` is used instead of `let`.
This allows, for example, the following:
```
let. let 1
print_int(let)
```
Since `let` doesn't end with a period, it doesn't collide with the keyword.
While there isn't much reason to do this with `let`, this feature is useful for other names, such as `type`.
Additionally, it means new keywords can be added without breaking older programs.

## Mutable variables

Variables created with `let.` are immutable, that is, they can't have their values reassigned.
To create a mutable variable, use `let. mut,`. Such a variable's value can be modified with a `set.` statement.
```
let. mut, x 1
print_int(x)
set. x 2
print_int(x)
```
This program will print `1`, followed by `2`.

`mut,` is a secondary keyword, distinguished by ending in a comma.
Secondary keywords are used within expressions starting with a primary keyword to further specify the expression's behavior.

## Comments

Comments start with the `※` character and continue until the end of the line.
```
print_int(1) ※ this will print the integer '1'
```

## Calling functions

We've already seen an example of a function call when calling the `print_int` function.
A function call is written with the function followed by the arguments in parentheses, separated by whitespace.
Here's how to call a function with multiple arguments:
```
let. x read_int()
let. y +(x 1)
print_int(y)
```

First, the `read_int` function is called with no arguments, and the variable `x` is bound to the call's result.
The function `read_int` reads an integer from standard input and returns it.

Then, the function `+`, which takes two integer arguments and returns their sum, is called with `x` and `1` as arguments.
This sets the variable `y` to the sum of `x` and `1`.
Note that Hagane doesn't have operators and uses regular functions instead.
`+(x 1)` in Hagane is equivalent to `x + 1` in most other languages.

Finally, the function `print_int` is called to display the value of the variable `y`.
In summary, the program takes an integer from the user and prints that integer plus one.

Several arithmetic functions are built into the language: these are
* `+` - addition
* `-` - subtraction
* `*` - multiplication
* `/` - integer division
* `%` - modulo

Note that the result of `/` is always rounded down, and the result of `%` has the same sign as the second argument.
This is different from a number of other programming languages.

When calling a function, the function is guaranteed to be evaluated first, followed by the arguments going from left to right.

## Special expressions

`let.` and `set.` are examples of special expressions, which are constructs that begin with a primary keyword.
Special expressions are the basis around which Hagane syntax is formed.

A special expression, in its most basic form, consists of a primary keyword followed by a series of clauses written between parentheses.
Each clause may be an expression, secondary keyword, colon, or label.

We've seen secondary keywords in use already.
A colon is just that - a colon: `:`.
A label is an expression preceded by a period, for example `.1`, `.x`, or `.f(x)`.

Examples of special expressions include:
```
let.(x 1)
let.(mut, x y)
set.(x 2)
if.(>(x 0) then, a else, b)
λ.(x y: *(x +(2 y)))
case.(opt_val .Some(val) val .None default)
```

As we've seen before, the parentheses may be dropped in certain contexts.

First, if the parentheses are dropped, the expression will last until the end of the line.
For example,
```
let.(mut, x 1)
set.(x 2)
```
is equivalent to
```
let. mut, x 1
set. x 2
```

To make an expression span multiple lines, indent all lines after the first one.
The previous example can also be written as
```
let.
    mut,
    x 1
set. x
    2
```

Also, if the primary keyword begins a line (that is, it is the first thing other than whitespace in a line of code),
the expression will also extend to non-indented lines that start with a secondary keyword, colon, or label.
For example,
```
case.(opt_val .Some(val) val .None default)
```
can be also written as
```
case. opt_val
.Some(val) val
.None default
```
But the same can't be done to write
```
let.(x case.(opt_val .Some(val) val .None default))
```
as
```
let. x case. opt_val
.Some(val) val
.None default
```
since `case.` does not begin a line. The labels will be associated with the `let.` instead, which forms an invalid expression.

Additionally, non-parenthesized special expressions may be included within parenthesized special expressions and function calls, where they will also end before the outer expression's closing parenthesis (unless one of the earlier rules also applies).
For example,
```
f(if.(x then, y else, z))
```
can also be written as
```
f(if. x then, y else, z)
```

## `do.` and scope

One important special expression is `do.`.
The expressions contained in a `do.` expression are executed in a separate scope - the variables defined inside the expression are not available outside it.
```
do.
    let. x 1
    ※ x is available here
    print_int(x)
※ end of scope - x no longer available
print_int(2)
※ print_int(x) - this would be a compilation error
```

There are other special expressions that allow creating scope by giving multiple statements.
For example, the value bound to the variable in a `let.` or `set.` expression may be the result of evaluating a scope.
```
let. x
    let. a read_int()
    let. b read_int()
    *(a b)
※ end of scope - a and b no longer available
print_int(x)
```
Here, x is bound to the product of two integers read from the user, but the variables the integers are stored in don't persist past the end of the indented scope.
The value of the last expression in a scope (here `*(a b)`) becomes the value of the entire scope.

## Additional syntax

There exists alternative syntax for some function calls and special expressions.

Function calls may be written without parentheses by following the function with a `‡` symbol.
For example, `f(x y)` can be written as `f‡ x y`.
The same rules apply as for unparenthesized special expressions.

Function calls with only one argument can be written with a `†` symbol.
For example, `f(x)` can be written as `f†x` (or `f† x` - whitespace is allowed).
Since there is always only one argument, there is no need for a terminator like a closed parenthesis.

Similar syntax is available for special expressions with only one argument by replacing the period with a `!`.
Special expressions with zero arguments also have a shorter syntax, written by replacing the period with a `¡`.

The following table sums up all the options:

|| Function call | Special expression
|---|---|---
Parenthesized | `f(x y)` | `f.(x y)`
Unparenthesized | `f‡ x y` | `f. x y`
One argument | `f†x` | `f!x`
Zero arguments | `f()` | `f¡`

## Defining functions

A function can be created with a `λ.` (lambda) expression.
For example, the following program uses a function to print the (rounded-down) average of two user-provided numbers.
```
let. avg λ. x y: /(+(x y) 2)

let. n1 print_int()
let. n2 print_int()
print_int(avg(n1 n2))
```

The variable `avg` is bound to the value of the expression `λ. x y: /(+(x y) 2)`.
This expression creates a function taking the arguments given before the colon - `x` and `y`.
When provided with arguments as values for `x` and `y`, the function returns the value given in its body, that is `/(+(x y) 2)`.

As with other expressions, the body of a `λ.` expression may be (and ususally is) indented, which allows placing multiple statements inside it.
```
let. avg λ. x y:
    let. sum +(x y)
    /(sum 2)
```

Since Hagane is a functional language, functions are first-class objects and may be used in the same ways as other values.
As such, they don't have to be immediately assigned to a variable - they can be freely passed to, returned from, and created inside other functions.
```
let. apply_twice λ. f x:
    f(f(x))

print_int(apply_twice(λ.(x: *(2 x)) 5))
```
Here, the function `apply_twice` takes a function and a value and applies the function to the value twice.
Applying the function `λ.(x: *(2 x))`, which doubles a number, to `5` twice will yield `20`.

## Types

Hagane is a statically-typed language. This means that every expression and variable has a type determined at compile time.

Hagane contains three built-in types: `Int`, `Bool`, and the polymorphic type `List`.

`Int` is a signed 64-bit integer. Any overflow in arithmetic operations results in an error.

To write a negative integer literal, put the minus sign after the number as a `~`.
For example, -1 will be written as `1~`.

`Bool` represents a true/false value, denoted respectively as `⊤`/`⊥`. Note that these are not keywords, but names of variables.

`List` is polymorphic, and therefore can't be used on its own. What this means is explained in a later section.

Hagane uses type inference - the types of expressions and variables are determined by the compiler based on the available information.
As such, most of the time there is no need to manually specify the types of variables.

But for those cases where it's useful or necessary to do so, type annotations exist.
A type annotation has the form of `∈.(<variable> <type>)`.
For example, our earlier `avg` function may be written like this:
```
let. avg λ. ∈.(x Int) ∈.(y Int):
    /(+(x y) 2)
```
This makes it explicit that `x` and `y` both have type `Int`.

Functions also have types. The type of a function taking n arguments of types `<arg_1>` to `<arg_n>` and returning a value of type `<ret>` is written as `⇒.(<arg_1> ... <arg_n> : <ret>)`.
For example, our `avg` function has type `⇒.(Int Int : Int)` and the function `read_int` has type `⇒.(: Int)`.

## Tuples

A tuple is a collection of values, possibly of different types. The type of a tuple describes which elements the tuple may contain.

An example of a tuple is `*.(4 ⊥ 10)`. The type of this tuple is `*.(Int Bool Int)` - the type of tuples consisting of an `Int`, `Bool`, and `Int`.

The values from which a tuple is constructed may be arbitrary expressions - for example, if `x` and `y` are both variables of type `Int`, `*.(x y +(x y))` is a tuple of type `*.(Int Int Int)`.

A tuple can be deconstructed into its components using a `let.` expression.
```
let. tuple *.(1 ⊤)
let. *.(x y) tuple
```
Here, the variable `x` is bound to the first element of `tuple`, in this case `1`, and `y` is bound to the second element, in this case `⊤`.

This is actually a special case of a more general concept - a pattern.
The variables inside a tuple pattern may actually be replaced with any pattern, including another tuple pattern.
```
let. nested_tuple *.(1 *.(2 3))
let. *.(x *.(y z)) nested_tuple
```
Here, `x`, `y`, and `z` will be bound to `1`, `2`, and `3` respectively.

Aside from variables and pattern tuples, another useful type of pattern is the wildcard pattern, written as `_`.
It behaves similar to a variable pattern, except it doesn't create a variable.
It's useful for ignoring parts of data.
```
let. *.(x _) tuple
```
Here, the first element of the tuple is bound to `x`, and the second is ignored.

Also, function parameters don't have to be identifiers either, they can actually be arbitrary patterns.
```
let. fst λ. *.(x _): x
```
This function extracts the first element from a two-element tuple.

One particular case of a tuple is `*¡` (`*.()`) - the tuple with no elements (typically pronounced "unit").
Such a tuple has type `*¡` (distinct from the tuple itself!), which only contains the value `*¡`.
Since a value of this type can only be `*¡`, it carries no information.

It's primarily used as the return type for functions that return any meaningful value, in a way analogous to `void` in many programming languages.
For example, the `print_int` function has type `‡(Int : *¡)`.

## `if.`

The `if.` expression can be used to create branching code.
```
let. x read_int()
let. y read_int()
let. max if. ≥(x y) then, x else, y
print_int(max)
```
This program reads two integers from the user and prints the larger of them.

The `≥` function returns a `Bool` indicating whether its first argument is greater or equal to its second argument.
The expression `if. ≥(x y) then, x else, y` evaluates to `x` if `≥(x y)` and to `y` otherwise.

Each of the three parts of an `if.` expression may consist of multiple statements forming a scope.
If the condition is only a single expression (as it usually is), the `then,` may be omitted:
```
if. ≥(x y)
    print_int(x)
    print_int(y)
else,
    print_int(y)
    print_int(x)
```
This expression prints `x` and `y` in order.

And if all three parts are only a single expression, both the `then,` and `else,` may be omitted.
```
let. max if. ≥(x y) x y
```

Six comparison functions are built into the language: these are `=`, `≠`, `<`, `≤`, `>`, `≥`.

To do multiway branching, additional `if,` (note the comma) and `then,` parts are added.
```
let. sgn λ. ∈.(n Int):
    if. >(n 0) then, 1
    if, <(n 0) then, 1~
    else, 0
```
The `sgn` function defined here returns the sign of the input (`1` for positive numbers, `1~` for negative, `0` for zero).

As before, if a condition is only one expression, the `then,` may be omitted:
```
let. sgn λ. ∈.(n Int):
    if. >(n 0) 1
    if, <(n 0) 1~
    else, 0
```

Alternatively, a syntax using labels is available:
```
let. sgn λ. ∈.(n Int):
    if.
    .>(n 0) 1
    .<(n 0) 1~
    else, 0
```

## Other control flow constructs

A `while.` loop executes its body as long as the condition is true.
```
let. mut, i read_int()
while. ≥(i 0)
    print_int(i)
    set. i -(i 1)
```
This program reads an integer from the user and counts down from it to zero.

Like with `if.`, the condition of a `while.` loop may consist of multiple statements, in which case it is separated from the body with a `do,` keyword.

`∧.` ("and") and `∨.` ("or") expressions take several expressions of type `Bool`.
`∧.` returns `⊤` if they all evaluate to `⊤`, and `⊥` otherwise.
`∨.` returns `⊤` if at least one evaluates to `⊤`, and `⊥` otherwise.

These expressions have the typical short-circuiting behavior where the expressions are evaluted in order.
For `∧.`, if one of the expressions evaluates to `⊥`, `⊥` is immediately returned and the following expressions will not be evaluated.
For `∨.`, if one of the expressions evaluates to `⊤`, `⊤` is immediately returned and the following expressions will not be evaluated.

## Custom types

Custom types may be defined with the `type.` statement.
```
type. Shape
    Point
    Circle(Int)
    Rectangle(Int Int)
```
The `Shape` type is declared as having three variants.
`Point`, `Circle`, and `Rectangle` are the variants' constructors, used to construct values of type `Shape`.
The types written between parentheses are the parameters that the constructors take.
`Point` has no parentheses, as it takes no arguments and is already a value on its own.

`Point`, `Circle(3)`, and `Rectangle(2 10)` are examples of expressions of type `Shape`.

A value with a custom type can be deconstructed with a `case.` expression.
```
let. area λ. ∈.(shape Shape):
    case. shape
    .Point 0
    .Circle(r) *(3 *(r r))
    .Rectangle(w h) *(w h)
```
This function takes a shape and returns an approximation of its area (approximating π as 3).

A `case.` expression matches a value to a series of patterns. and returns the body associated with the first one that matches.
For example, if `shape` is `Circle(4)`, the `Point` pattern will fail to match.
The next pattern, `Circle(r)`, matches, so the variable `r` is bound to `4`.
`*(3 *(r r))` is evaluated, giving `48`, which is returned as the value.

The patterns in a `case.` expression must be exhaustive.
That is, any possible value of the matched expression has to match at least one pattern.

All patterns mentioned in the section on `let.` may also be used in `case.` expressions.
```
let. are_both_points λ. ∈.(shapes *.(Shape Shape)):
    case. shapes
    .*.(Point Point) ⊤
    ._ ⊥
```

Variant patterns may also be used in `let.` expressions.
However, the single pattern in a `let.` expression must be exhaustive by itself.
That means only types with a single variant may be deconstructed with a `let.` expression.
```
type. Point2D Point2D(Int Int)

※ assume point is some Point2D
let. Point2D(x y) point
```
The same applies to patterns in function parameters.

Another type of pattern that may fail is the integer pattern, which only matches an `Int` with a given value.
```
let. is_zero λ. n:
    case. n
    .0 ⊤
    ._ ⊥
```
```
let. is_origin λ. point:
    case. point
    .Point2D(0 0) ⊤
    ._ ⊥
```

Also, the built-in type `Bool` is defined as a type with two variants:
```
type. Bool
    ⊥
    ⊤
```
Therefore, it is possible to match on it:
```
let. ∧ λ. ∈.(x Bool) ∈.(y Bool)
    match. *.(x y)
    .*.(⊤ ⊤) ⊤
    ._ ⊥
```

## Polymorphism

Consider the following function:
```
let. swap λ. *.(x y): *.(y x)
```
It takes a two-element tuple and swaps its elements. But what is its type?
The argument to this function can be of type `*.(T U)` for any types `T` and `U`, and the return value is then of type `*.(U T)`.
So the function has type `⇒.(*.(T U) : *.(U T))` for all types `T` and `U`.
The function is said to be polymorphic over these types.

Any value can be polymorphic, not just functions, but polymorphism is most commonly encountered in functions.

Values are allowed to be polymorphic only when bound to immutable variables.
When a polymorphic variable is used, it is instantiated to some specific version of itself, with the types it's polymorphic over filled in appropriately.
For example, in the expression `swap(*.(2 ⊥))`, the function `swap` is instantiated to have type `⇒.(*.(Int Bool) : *.(Bool Int))`.

A value's type will automatically be inferred as the broadest possible type when it's bound to a variable.
The type can be manually narrowed with type annotations.

To manually specify that the type of a variable is polymorphic, use the `∀,` keyword in the `let.` statement.
```
let. ∀, T U: λ. ∈.(*.(x y) *.(T U)):
    *.(y x)
```

A type can also be polymorphic.
To declare a polymorphic type, use the `∀,` keyword in the `type.` statement.
```
type. ∀, T: BinTree
    Leaf
    Node(T BinTree(T) BinTree(T))
```
This is the type of a binary tree with values of type `T` in its nodes.
`BinTree` is a polymorphic type - its instance for a particular type `T` is called `BinTree(T)`.

## Lists

A list is a collection of values of the same type.
A list can be created with the `list.` expression.
```
list.(1 2 3 4 5)
```
This is a list of integers from 1 to 5. It has type `List(Int)`.
`List` is a polymorphic type, so a list of values of type `T` has type `List(T)`.

There are a few basic operations on lists built into the language.

`empty` is a list with no elements, equivalent to `list.()`.
It's an example of a polymorphic value that's not a function.

`len(list)` gives the number of elements in `list`.

`get(list i)` gives the element at index `i`. Indices start at 0.

`put(list i elem)` gives `list` with the element at `i` replaced with `elem`.

`push(list elem)` gives `list` with `elem` appended to the end.

`pop(list)` gives `list` without its last element.

Trying to access a list through `get` or `put` with an index outside its bounds or calling `pop` on an empty list will cause the program to exit with an error.

Note that `put`, `push`, and `pop` return a new list and don't modify the list passed into them in any way.
```
let. a list.(1 2 3)
print_int(len(a)) ※ 3
push(a 4) ※ a remains unchanged
print_int(len(a)) ※ still 3
set. a push(a 4) ※ this is the correct way to add an element to a list
print_int(len(a)) ※ 4
```

`list.` can also be used as a pattern.
```
let. has_one_element λ. list:
    case. list
    .list.(_) ⊤
    ._ ⊥
```

## Characters and strings

Hagane currently has no dedicated character or string type, but it has character and string literals.
A character literal is written as `#<character>` and has type `Int`. Its value is the Unicode codepoint of the character.
For example, `#A` has value `65`, `#π` has value `960`, and `# ` has value `32`.

A string literal is written by placing the string contents between double quotes (").
It has type `List(Int)` and is equivalent to the list of the characters making it up.
For example, `"hello"` is equivalent to `list.(#h #e #l #l #o)` and further to `list.(104 101 108 108 111)`.

Character and string literals may also be used as patterns, where they are pretty much equivalent to integer and list patterns.

Character and string literals may contain escape sequences.
These are: `\n` for the newline character, `\"` for the double quote character (`"`), and `\\` for the backslash itself (`\`).

## Other features

`unreachable` is a function of type `⇒.(: T)` for all `T`.
It's meant to be used code that's unreachable, but can't be proven as such by the compiler.
When `unreachable()` is evaluated, the program exits with an error message.

`print_byte` and `read_byte` are functions similar to `print_int` and `read_int`, but they allow printing and reading individual bytes.
`read_byte` returns `1~` on end of file.
```
let. mut, b read_byte()
while. ≠(b 1~)
    print_byte(b)
    set. b read_byte()
```
This program prints back everything typed into it.

For reading command-line arguments three functions exist:
* `get_argc()` returns the number of arguments (`argc`).
* `get_argv_len(i)` returns the length of the `i`th argument (`strlen(argv[i])`).
* `get_argv_byte(i j)` returns the `j`th byte of the `i`th argument (`argv[i][j]`).

The `extern.` statement is used to declare variables defined in external modules, written in a language like C.

Its syntax is `extern. <name> <type>`.
The type must be a function taking some (possibly zero) number of integer arguments and returning either an integer or `*¡`.
The `Int` type in Hagane corresponds to the `int64_t` type in C, and `*¡` corresponds to `void`.

For how to include external modules, see the chapter on command-line arguments.

## Command-line arguments

The Hagane compiler takes a list of command-line arguments.
Those starting with a `-` character are interpreted as options, and all the other ones are names of input files.
All the input files are concatenated before compilation in the order they are given in.

The following command-line options are available:
* `-O<n>`, where `<n>` is an integer from `0` to `3` - sets the optimization level to `<n>`. The default level is 0.
* `-print-ir-unopt`, `-print-ir` - used for debugging to print the generated LLVM IR, respectively before or after optimization.
* `-o<name>` - sets the output filename to `<name>`. The default is `out`.
* `-L<arg>` - `<arg>` is passed as an argument to `clang` during linking.
    Can be used to pass names of additional object files to link them to the program.

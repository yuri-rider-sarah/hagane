# Hagane Language Reference (version 0.0.4)

## 1. Tokens
A program consists of a sequence of tokens. Whitespace characters may occur anywhere between tokens, and are used to separate them.

### 1.1. Identifiers
An identifier may consist of characters with Unicode General Category L\* (Letter), M\* (Mark), N\* (Number), Pc (Punctuation, Connector), Pd (Punctuation, Dash), or S\* (Symbol).
The following characters from category Po (Punctuation, Other) are also permitted: `%&*/\؉؊٪‰‱′″‴‵‶‷⁂⁗⁎⁑⁕﹠﹡﹨﹪％＆＊／＼`
However, identifiers may not begin with a numeric character (with General Category N\*) or consist of a single undescore.

### 1.2. Keywords
Keywords are distinguished from identifiers by ending in a period or a comma.
The rules for which characters may occur in a keyword before the period are the same as for identifiers.

Primary keywords end in a period, while secondary keywords end in a comma.

### 1.3. Integer literals
Integer literals consist of digits `0`-`9`.

### 1.4. Other tokens
The following characters represent tokens: `(`, `)`, `[`, `]`, `{`, `}`, `'`, `:`, `#`, `.`.

They don't need to be separated from other tokens with whitespace, except for `#` and `.`, which have to be separated from the left.

An identifier consisting of single undescore is a special token, acting as a wildcard in pattern matching expressions.

### 1.5. Comments
Comments are marked with the character `※` and last until the end of the line.

## 2. Types

### 2.1. Basic types
There are two basic builtin types: `Int` and `Bool`.
- `Int` represents 64-bit signed integers.
- `Bool` represents true/false values, denoted respectively as `⊤`/`⊥`.

There is also a polymorphic builtin type `List`:
- `List[T]` represents a list whose elements have type `T`.

### 2.2. Tuple types
```
#(<type_1> ... <type_n>)
```
This is the type of tuple of n elements having types `<type_1>` through `<type_n>`.

Of particular note is the empty tuple `#()`, also called the unit type, which contains only one value, also written `#()`.
It is used as a return type for functions which do not return a meaningful value.

Nonempty tuples are useless in the current version, as there is no way to deconstruct them.

### 2.3. Function types
```
‡(<type_1> ... <type_n> : <type_ret>)
```
This is the type of a function taking n arguments of types `<type_1>` through `<type_n>` and returning a value of type `<type_ret>`.

### 2.4. Custom types
Custom types can be defined with the `type.` expression.

## 3. Syntax
The basic element of Hagane syntax is the expression. Each expression evaluates to a value.

### 3.1. Basic expressions
Identifiers evaluate to the value of the variable they represent.
Integer literals evaluate to the integer they represent.

### 3.2. Function call
```
<expr_f>(<expr_1> ... <expr_n>)
```
The value of `<expr_f>` is called with the values of `<expr_1>` through `<expr_n>`.
The result is the return value of the call.

The function and arguments are evaluated left-to-right.

### 3.3. Tuple construction
```
#(<expr_1> ... <expr_n>)
```
This creates an n-tuple containing the values of `<expr_1>` through `<expr_n>`.

The subexpressions are evaluated left-to-right.

### 3.4. Special expressions
Special expressions are used for all other expressions. A special expression consists of a primary keyword and a series of clauses, where each clause is either a secondary keyword, a label, or a block.

#### 3.4.1. Block syntax
A block consists of a series of expressions, either surrounded by braces or indented. A block consisting of a single expression may also be written as just the expression.

To determine if a line starts an indented block, its indentation level is compared to the indentation level of the line containing the primary keyword of the special expression that would contain the block.

```
<outer context>
    <expr_1>
    ...
    <expr_n>
<outer context>
```
```
{<expr_1> ... <expr_n>}
```
```
<expr>
```

#### 3.4.2. Labels

A label consists of a `.` token followed by an expression.

#### 3.4.3. Block evaluation

How a block is interpreted depends on the special expression to which it belongs.
However, many special expressions will use blocks in the same way, which is referred to as evaluating them.

To evaluate a block, each expression is evaluated, and the value of the last expression is returned. A block with no expressions evaluates to `#()`.

#### 3.4.4. Standard special expression syntax
```
<keyword>(<clause_1> ... <clause_n>)
```
This is the standard syntax for special expressions.

#### 3.4.5. Block special expression syntax
```
<keyword> <clause_1> ... <clause_n>
```
A special expression may be written without the parentheses at the top level of a block or program.

The first line after the primary keyword that doesn't continue an earlier clause or start with a secondary keyword or label is considered to belong to the next expression.

For example, the following consists of two expressions — an `if.` special expression and a call to `print`:
```
if. >(x 0) then,
    set. x -(x 1)
    print(x)
else, print(0)
print(y)
```

#### 3.4.6. Clause special expression syntax
```
<keyword> <clause_1> ... <clause_n>
```
A special expression may also be written without the parentheses when it occurs as a clause belonging to a block special expression or another clause special expression.

The first line after the primary keyword that doesn't continue an earlier clause is considered to belong to the next expression.

Part of the expression may be indented. Lines within the indented part may start with a secondary keyword or label or continue an eariler clause.
The end of the indented part marks the end of the expression.

For example, the following code contains two `if.` expressions, one nested inside the second block of another:
```
if. =(y 0) then, if. >(x 0)
    then, x
    else, 0
else, y
```

### 3.5. Type annotation
```
<expr> '<type>
```
Most expressions may be followed by a type annotation. It indicates that the type of `<expr>` is `<type>`.

Block and clause special expressions may not have a type annotation.

It's intended to be used in situations where the type cannot be inferred automatically and therefore has to be specified manually.

### 3.6. Type arguments
```
<var>[<type_arg_1> ... <type_arg_n>]
```
The polymorphic variable `<var>` is instantiated with the type arguments `<type_arg_1>` through `<type_arg_n>`.

Due to limited type inference in the current version all uses of polymorphic variables must be given explicit type arguments.

# 4. Builtin special expressions

Names of blocks are written in triangle brackets. Optional parts of expressions are written in square brackets. All secondary keywords not written in square brackets are optional and don't influence the meaning of the expression.

### 4.1. `do.`
```
do. <block>
```
The block `<block>` is evaluated.

### 4.2. `type.`
```
type. [∀, <type_params>] <name> {<variant_1> ... <variant_n>}
```
Each `<variant_k>` must be of the form `<variant_name_k>(<type_k_1> ... <type_k_m>)`, where each `<type_k_l>` is a valid type.

Declares a type with `n` variants, the `k`th containing elements of types `<type_k_1>` through `<type_k_l>`.

If `∀, <type_params>` is used, the type and its variants will be polymorphic, taking type parameters `<type_params>`.

### 4.3, `match.`
```
match. <expr> .<pattern_1> <case_1> ... .<pattern_n> <case_n>
```
Each `<pattern_k>` must be a valid pattern, and all `<case_k>` blocks must have the same type.

`<expr>` is evaluated and matched against each pattern in the order that they appear.
For the first pattern `<pattern_k>` that matches, the `<body_k>` block associated with it is evaluated with the appropriate bindings and its value returned from the expression.

If no pattern matches, an error occurs.

#### 4.3.1. Patterns
A pattern is an integer literal, identifier, wildcard, tuple of patterns or variant pattern.

An integer literal pattern matches an integer of the same value.

An identifier pattern matches any value, binding it to the identifier.

A wildcard pattern matches any value, without creating any bindings.

A tuple pattern has form `#(<pattern_1> ... <pattern_n>)`.
It matches tuples of `n` elements, matching each element to the appropriate subpattern.

A variant pattern has form `<variant_name>(<pattern_1> ... <pattern_n>)`.
It matches instances of the variant named `<variant_name>`, matching each element to the appropriate subpattern.

### 4.4. `let.`
```
let. [mut,] [∀, <type_params>] <pattern> <expr>
```
`<pattern>` must be a valid pattern.

`<expr>` is matched against `<pattern>` and the appropriate bindings created.

If the pattern doesn't match, an error occurs.

If `mut,` or `∀,` is used, `<pattern>` must be an identifier.

If `mut,` is used, the declared variable will be mutable.

If `∀, <type_params>` is used, the declared variable will be polymorphic, taking type parameters `<type_params>`.

The same variable may be declared multiple times, in which case the later declarations shadow earlier ones.

A variable cannot be both polymorphic and mutable.

The type of this expression is `#()`.

### 4.5. `set.`
```
set. <identifier> <expr>
```
`<identifier>` must consist of a single identifier.

This changes the value of variable `<identifier>` to the value of `<expr>`.

The type of this expression is `#()`.

### 4.6. `if.`
```
if. <cond> then, <consequent> else, <alternative>
```
`<cond>` is a block of type `Bool`, while `<consequent>` and `<alternative>` are blocks of the same type.

First, `<cond>` is evaluated.
If the resulting value is `⊤`, `<consequent>` is evaluated and its value returned.
Otherwise the resulting value is `⊥`, in which case `<alternative>` is evaluated and its value returned.

### 4.7. `cond.`
```
cond. .<test_1> <body_1> ... .<test_n> <body_n>
```
Each `<test_k>` must have type `Bool`, and all `<body_n>` blocks must have the same type.

Each `<test_k>` is evaluated in order.
For the first test `<pattern_k>` that evaluates to `⊤`, the `<body_k>` block associated with it is evaluated with the appropriate bindings and its value returned from the expression.

If no condition holds, an error occurs.

### 4.8. `while.`
```
while. <cond> do, <body>
```
`<cond>` is a block of type `Bool`, and `<body>` is a block of any type.

`<body>` is repeatedly executed as long as `<cond>` evaluates to `⊤`.

The type of this expression is `#()`.

### 4.9. `λ.`
```
λ. <vars> ⇒, <body>
```
The block `<vars>` must consist of variables.

This evaluates to an function that takes as parameters the variables composing `<vars>` and returns the value of the block `<body>`.

Due to limited type inference in the current version, all `λ.` expressions must have type annotations on all their parameters and the last expression of the body.

## 5. Builtin functions
Several basic functions are built into the language.

### 5.1. Arithmetic functions
```
+ '(Int Int : Int)
- '(Int Int : Int)
* '(Int Int : Int)
/ '(Int Int : Int)
% '(Int Int : Int)
```
These functions perform basic arithmetic on integers.

### 5.2. Comparison functions
```
= '(Int Int : Bool)
≠ '(Int Int : Bool)
< '(Int Int : Bool)
≤ '(Int Int : Bool)
> '(Int Int : Bool)
≥ '(Int Int : Bool)
```
These functions perform basic integer comparisons.

### 5.3. Lists
```
empty '†(T) List(T)
len '†(T) ‡(List(T) : Int)
get '†(T) ‡(List(T) Int : Int)
put '†(T) ‡(List(T) Int T : List(T))
push '†(T) ‡(List(T) T : List(T))
pop '†(T) ‡(List(T) : List(T))
```

Here, `†(T)` means "for every type `T`".

`empty` is a list with no elements.

`len` returns the length of a list.

`get` returns the element at a given index in a list.

`put` returns a list where the element at a given index is replaced with the specified value.

`push` returns a list with the given value appended to the end.

`pop` returns the given list without the last element.

If an index given to `get` or `put` is out of the list's bounds, or if `pop` is called on an empty list, an error message is printed to the standard error and the program fails.

### 5.4. Input/output
```
print '(Int : #())
read '(: Int)
```

`print` prints an integer to the standard output, followed by a newline.

`read` reads an integer from the standard input. If an integer cannot be read, an error message is printed to the standard error and the program fails.

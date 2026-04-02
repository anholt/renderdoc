import ast
import inspect
import sys
import struct
import builtins
from typing import List, Dict, Any, Tuple, Callable, TypeVar, Optional


# return whether an object is a specialisation of the given generic,
# e.g. _is_generic(Dict, Dict[str, int]) == True
def _is_generic(generic, obj):
    if hasattr(obj, "__origin__") and hasattr(generic, "__origin__"):
        if generic.__origin__ == obj.__origin__:
            return True

    if hasattr(obj, "__origin__"):
        return obj.__origin__ == generic

    return False


# return true for AST nodes that need their own scope - this is module level, then
# classes and functions which may be nested inside each other
def _is_scope_node(node):
    return (
        isinstance(node, ast.Module)
        or isinstance(node, ast.ClassDef)
        or isinstance(node, ast.FunctionDef)
        or isinstance(node, ast.AsyncFunctionDef)
    )


# get all the return statements immediately inside a function without going into nested
# functions
def _get_return_statements(node: ast.AST, root: bool = True):
    if _is_scope_node(node) and not root:
        return []

    if isinstance(node, ast.Return):
        return [node]

    ret = []

    for recurse in ["body", "orelse", "finalbody"]:
        if hasattr(node, recurse):
            for n in getattr(node, recurse):
                if not _is_scope_node(n):
                    ret += _get_return_statements(n, False)

    return ret


# Python 3.8+ is expected to have start and end lines.
# Before that, end was missing so we assume all statements are single
# line (or the last child).
def _get_linerange(node: ast.AST):
    if not hasattr(node, "lineno"):
        return (-1, -1)

    lineno = getattr(node, "lineno")

    if hasattr(node, "end_lineno"):
        return (lineno, getattr(node, "end_lineno"))

    end_lineno = lineno
    for recurse in ["body", "orelse", "finalbody"]:
        if hasattr(node, recurse) and len(getattr(node, recurse)) > 0:
            end_lineno = max(end_lineno, _get_linerange(getattr(node, recurse)[-1])[1])

    return (lineno, end_lineno)


# Python 3.8+ is expected to have start and end columns.
# Before that, end was missing so we assume all statements are
# extremely long
def _get_colrange(node):
    if not hasattr(node, "col_offset"):
        return (-1, -1)

    return (node.col_offset, getattr(node, "end_col_offset", 9999))


# true if this is a `self.foo` type attribute lookup, so we know
# to look up in the parent scope which is how we handle self
def _is_self_lookup(parsed: ast.AST):
    return (
        isinstance(parsed, ast.Attribute)
        and isinstance(parsed.value, ast.Name)
        and parsed.value.id == "self"
    )


# comment out all lines starting from a given point,
# to try and make things compile. Stops when it hits
# an indent that looks like the end of the statement
def _commentlines(text, first_comment_line):
    lines = text.splitlines()

    indent = len(lines[first_comment_line]) - len(lines[first_comment_line].lstrip())
    lines[first_comment_line] = "#" + lines[first_comment_line]
    for i in range(first_comment_line + 1, len(lines)):
        if lines[i].strip() == "":
            continue
        if lines[i].startswith(" " * (indent + 1)):
            lines[i] = "#" + lines[i]
            continue
        break

    return "\n".join(lines)


# a given instance of an identifier and its type
class Ident:
    # first line this ident is valid
    line: int = -1
    # for assignments the identifier is only valid on the first line
    # before a certain column.
    # mostly relevant for overwriting assignments e.g. foo = foo.bar
    # so the idents for LHS foo and RHS foo can be differentiated
    col: int = 9999
    # the type or type hint
    type_obj: Optional[Any] = None
    # for functions without return annotations, this will be set to the AST node
    # for lazy evaluation to obtain a guessed return type.
    # We do it this way as we normally process in declaration order but
    lazy_node: Optional[ast.AST] = None


# a scope - either a module, class or function
class Scope:
    # the name for debugging
    name: str
    # the parent scope, for searches upwards for identifiers
    parent: "Optional[Scope]" = None
    # the parsed node
    parsed: ast.AST
    # the type, only relevant for classes
    type_obj: Optional[Any] = None
    # known identifiers in this scope
    identifiers: Dict[str, List[Ident]]
    # for non-modules, the ident of this scope
    ident: Optional[Ident] = None
    # whether this scope is a class or not (for finding `self`)
    is_class: bool = False

    def __init__(self):
        self.identifiers = {}

    def set_ident(self, name: str, ident: Ident):
        if name not in self.identifiers:
            self.identifiers[name] = []
        self.identifiers[name] += [ident]

    # look up the version of an identifier on a given line, in our parents,
    # or in the builtins
    def get_ident(self, name: str, line: int, col: int):
        ret = None
        if name in self.identifiers:
            for i in self.identifiers[name]:
                # only consider identifiers that are valid for the line & col
                # we're searching for
                if (
                    i.line < line
                    or (i.line == line and (col < i.col or col == -1))
                    or line == -1
                ):
                    # if we don't have a match, or this match is more recent, use it
                    if ret is None or ret.line < i.line:
                        ret = i

        # if we don't have a record of it, or we're at a statement before
        # the first assignment, search the parent at our declaration line
        if ret is None:
            if self.parent is not None:
                return self.parent.get_ident(
                    name, self.ident.line if self.ident is not None else line, col
                )

            if name in dir(builtins):
                return getattr(builtins, name)

            return None

        return ret

    def full_name(self):
        if self.parent is not None:
            return f"{self.parent.full_name()}::{self.name}"
        return self.name

    def __repr__(self):
        return f"<Scope '{self.full_name()}'>"


# Main class, reflects a given source text (if it can) and allows
# lookups of the types of expressions as well as auto-completion
# of partial expressions
class PyReflector:
    def __init__(self, text: str, starting_globals: Dict[str, Any], debug_types: bool):
        # the parsed module, or None if parsing completely failed
        self.module: Optional[ast.Module]

        # for tests - a lookup to retrieve the actual typevar since they can't be compared by name
        self.user_types: Dict[str, TypeVar] = {}

        # the text that was actually parsed, including any truncation/commenting needed
        # to get it to compile
        self.parsed_text: str

        # an error if parsing completely failed
        self.parse_error: Optional[str]

        # the starting set of globals to consider the module populated with
        self.starting_globals = starting_globals
        if self.starting_globals is None:
            self.starting_globals = globals()

        # whether or not type-processing should be debugged. Instead of falling back
        # to `typing.Any` for unknown types, instead a string bounded by "@@" is returned.
        # Mostly for internal use
        self.debug_types = debug_types

        # the current scope for each line
        self.scopes: List[Scope] = []

        # a pending FIFO which we process classes and functions in. This is used
        # so that if we want to infer a type we can steal it early and process functions
        # out of normal declaration order. Can't resolve mutually-recursive functions
        # that require type guessing but improves many common situations where a class
        # method calls another that's declared later and doesn't have proper type
        # annotations
        self.pending: List[Tuple[Optional[Scope], ast.AST]] = []

        # try to parse the text
        self._parse_text(text)

        # This checks module against None internally to help type checkers
        self._process_module()

    def _parse_text(self, text: str):
        # try a simple parse. If there are no syntax errors this will
        # succeed.
        try:
            self.module = ast.parse(text)
            self.parsed_text = text
            self.parse_error = None
            return
        except SyntaxError as err:
            if err.lineno is None:
                raise err
            first_comment_line = err.lineno - 1

        # when encountering an error, comment everything
        # from the error line to the next line with same or
        # less indent (excluding blank lines) and try again

        mod = _commentlines(text, first_comment_line)

        try:
            self.module = ast.parse(mod)
            self.parsed_text = mod
            self.parse_error = None
            return
        except SyntaxError as err2:
            if err2.lineno is None:
                raise err2

            # if the error has moved to a later line, that suggests the
            # original error was reported from some previous line, so we
            # should try from an earlier point
            # if not, we can't recover this
            if err2.lineno <= first_comment_line + 1:
                self.module = None
                self.parsed_text = mod
                self.parse_error = "Error remained after comments"

        # first see where we can truncate to and successfully parse
        # (up to 10 lines of non-blank lines truncated, to limit scope)

        lines = text.splitlines()

        trunc_lines = lines[0 : first_comment_line + 1]
        removed = 0
        while len(trunc_lines) > 0:
            if trunc_lines[-1].strip() == "":
                del trunc_lines[-1]
                continue

            del trunc_lines[-1]
            removed += 1

            if len(trunc_lines) == 0:
                break

            if trunc_lines[-1].rstrip()[-1] == ":":
                trunc_lines[-1] += " pass"
                lines[len(trunc_lines) - 1] += " pass"

            try:
                parsed = ast.parse("\n".join(trunc_lines))
                text = "\n".join(lines)
                break
            except Exception:
                if removed >= 10:
                    self.module = None
                    self.parsed_text = "\n".join(trunc_lines)
                    self.parse_error = "Couldn't backtrack"

        # now we know that trunc_lines parses,
        # retry commenting starting from there
        mod = _commentlines(text, len(trunc_lines))

        try:
            self.module = ast.parse(mod)
            self.parsed_text = mod
            self.parse_error = None
            return
        except Exception:
            self.module = None
            self.parsed_text = mod
            self.parse_error = "Error remained after comments"

    def valid(self):
        return self.module is not None

    # get the source string for a given line
    def get_line_source(self, line: int):
        return self.parsed_text.splitlines()[line - 1]

    def _type_failure(self, err: str):
        if self.debug_types:
            return "@@" + err.replace("@", "") + "@@"
        return Any

    def _get_type(
        self,
        scope: Scope,
        parsed: Optional[ast.AST],
        tmp_types: Optional[Dict[str, Any]] = None,
    ) -> Any:
        # simple protection and helps the type checker, silently drop None
        if parsed is None:
            return None

        # for things that are names, get the ident and look up its instance.
        # first check tmp_types for things like temporary objects inside list comprehensions
        # that we don't create proper identifiers for
        name = ""
        line, col = -1, -1
        if isinstance(parsed, ast.Name):
            name = parsed.id
            line, col = parsed.lineno, parsed.col_offset
        if isinstance(parsed, ast.arg):
            name = parsed.arg
            line, col = parsed.lineno, -1
        if isinstance(parsed, ast.alias):
            name = parsed.name
            if parsed.asname is not None:
                name = parsed.asname
            line = getattr(parsed, "lineno", -1)
            col = getattr(parsed, "col_offset", -1)

        if name != "":
            if tmp_types is not None and name in tmp_types:
                return tmp_types[name]
            ident = scope.get_ident(name, line, col)
            if ident is None:
                return self._type_failure(f"Unknown name {name}")
            if isinstance(ident, Ident):
                # if this is a function call that has a lazy_node, and it's in
                # our pending list (ie. not already on the current stack somewhere
                # due to mutual recursion) process it now so we can get a better
                # type object from inferring its return type
                if ident.lazy_node is not None:
                    for i, p in enumerate(self.pending):
                        if p[1] == ident.lazy_node:
                            self._process_pending(i)
                            break

                return ident.type_obj
            return ident

        if isinstance(parsed, ast.Attribute):
            # detect single-level self.foo and look up in parent if it exists
            # these identifiers are stored in the parent scope so they're available
            # to all self members
            if _is_self_lookup(parsed) and scope.parent is not None:
                self_lookup = scope.get_ident(
                    "self", parsed.value.lineno, parsed.value.col_offset
                )
                if self_lookup is not None:
                    # find the parent class, it could be multiple steps up if this is
                    # a nested function
                    parent_scope = scope.parent
                    while not parent_scope.is_class and parent_scope.parent is not None:
                        parent_scope = parent_scope.parent

                    ret = parent_scope.get_ident(parsed.attr, -1, -1)

                    if ret is None:
                        return ret

                    # if this is a function call that has a lazy_node, and it's in
                    # our pending list (ie. not already on the current stack somewhere
                    # due to mutual recursion) process it now so we can get a better
                    # type object from inferring its return type
                    if ret.lazy_node is not None:
                        for i, p in enumerate(self.pending):
                            if p[1] == ret.lazy_node:
                                self._process_pending(i)
                                break

                    return ret.type_obj

            # get the type of the base object that we're looking up
            base = self._get_type(scope, parsed.value, tmp_types)
            if isinstance(base, str):
                return self._type_failure(f"{base}, looking up {parsed.attr}")

            if base is None:
                if self.debug_types:
                    return self._type_failure(
                        f"Unexpected None in base {parsed.value} for access {parsed.attr}"
                    )
                return Any

            if not hasattr(base, parsed.attr):
                # if the base is a typevar, it's a user defined type let's
                # see if this is a member we know about
                if isinstance(base, TypeVar):
                    base_ident = scope.get_ident(
                        base.__name__, parsed.value.lineno, parsed.value.col_offset
                    )
                    if base_ident is not None:
                        base_scope = self.scopes[base_ident.line]

                        # if this scope is in our pending list then process it now
                        # so we can get a complete type object
                        for i, p in enumerate(self.pending):
                            if p[0] == base_scope:
                                self._process_pending(i)
                                break

                        attr_ident = base_scope.get_ident(
                            parsed.attr, parsed.value.lineno, parsed.value.col_offset
                        )

                        if attr_ident is not None:
                            return attr_ident.type_obj
                return self._type_failure(
                    f"Attribute {parsed.attr} not found in {parsed.value}"
                )

            ret = getattr(base, parsed.attr)

            # if this is a property return the type that calling the property getter would return
            if isinstance(ret, property) and ret.fget is not None:
                try:
                    ret = inspect.signature(ret.fget).return_annotation
                except:
                    return self._type_failure(
                        f"Failed to inspect property {parsed.attr}"
                    )

            # if this is a plain object return its type, if it's some kind of callable
            # then return the object directly as it is a type
            if (
                ret is not None
                and not inspect.isclass(ret)
                and not inspect.isfunction(ret)
                and not inspect.isbuiltin(ret)
                and not inspect.ismethod(ret)
                and not inspect.ismethoddescriptor(ret)
            ):
                if not hasattr(ret, "__origin__") or not inspect.isclass(
                    getattr(ret, "__origin__")
                ):
                    ret = type(ret)

            return ret

        if isinstance(parsed, ast.Expr):
            return self._get_type(scope, parsed.value, tmp_types)

        if isinstance(parsed, ast.Call):
            func = self._get_type(scope, parsed.func, tmp_types)

            # special case a bunch of builtins that don't have proper type
            # annotations. Not strictly needed as we don't expect to do anything,
            # but useful for debugging
            # if we could process typeshed stubs we could do away with this, but
            # those don't parse and need a dedicated separate parser
            if func is len:
                return int
            if func is range:
                return List[int]
            if func is max or func is min or func is reversed or func is sorted:
                return self._get_type(scope, parsed.args[0], tmp_types)
            if func is any or func is all or func is isinstance or func is issubclass:
                return bool
            if func is dir:
                return dict
            if func is cast:
                return self._get_type(scope, parsed.args[0], tmp_types)

            if func is enumerate:
                seq_type = self._get_type(scope, parsed.args[0], tmp_types)
                inner_type = Any
                if _is_generic(List, seq_type):
                    inner_type = seq_type.__args__[0]
                return Tuple[int, inner_type]

            if func is next:
                seq_type = self._get_type(scope, parsed.args[0], tmp_types)
                inner_type = Any
                if _is_generic(List, seq_type):
                    inner_type = seq_type.__args__[0]
                return inner_type

            if func is str.format:
                return str
            if func is list.index:
                return int
            if func is struct.unpack or func is struct.unpack_from:
                return Tuple[Any, ...]

            if _is_generic(Callable, func):
                ret = func.__args__[-1]
                if ret == type(None):
                    ret = None

                return ret

            if func is list:
                seq_type = self._get_type(scope, parsed.args[0], tmp_types)
                if _is_generic(List, seq_type):
                    return seq_type

            if type(func) is type or type(func) is TypeVar:
                return func

            if func is None:
                return self._type_failure(
                    f"Invalid None looking up callable {parsed.func}"
                )

            # if it doesn't fall into the above cases, try to use inspect to get it from the signature
            try:
                sig = inspect.signature(func)
                # assume this is a builtin with missing docs
                if sig.return_annotation == inspect.Signature.empty:
                    return Any
                return sig.return_annotation
            except:
                return self._type_failure(f"Failed to inspect signature of {func}")

        # unpack the elements in a tuple to generate the type for it
        if isinstance(parsed, ast.Tuple):
            params = tuple([self._get_type(scope, e, tmp_types) for e in parsed.elts])
            # if we have type failures enabled, returned types can be error strings
            if any([isinstance(x, str) for x in params]):
                return self._type_failure(f"Failed tuple[{params}]")
            return Tuple[params]

        if isinstance(parsed, ast.Dict):
            if len(parsed.keys) == 0:
                return Dict[Any, Any]
            keytypes = list(
                set([self._get_type(scope, e, tmp_types) for e in parsed.keys])
            )
            valtypes = list(
                set([self._get_type(scope, e, tmp_types) for e in parsed.values])
            )
            keytype, valtype = Any, Any
            if len(keytypes) == 1:
                keytype = keytypes[0]
            if len(valtypes) == 1:
                valtype = valtypes[0]
            return Dict[keytype, valtype]

        if isinstance(parsed, ast.List):
            if len(parsed.elts) == 0:
                return List[Any]

            x = self._get_type(scope, parsed.elts[0], tmp_types)
            if isinstance(x, str):
                return self._type_failure(f"Failed list[{x}]")

            return List[x]

        # subscripts could either be generic type declarations or indices into a
        # sequence type. We only handle standard generics
        if isinstance(parsed, ast.Subscript):
            coll_type = self._get_type(scope, parsed.value, tmp_types)

            slice = parsed.slice
            if sys.version_info < (3, 9):
                if isinstance(slice, ast.Index):
                    slice = slice.value

            # handle type annotations which directly subscript these generics in the AST
            # e.g. foo: List[int] = blah()
            if coll_type is List:
                return List[self._get_type(scope, slice, tmp_types)]
            if coll_type is Optional:
                return Optional[self._get_type(scope, slice, tmp_types)]
            if coll_type is Tuple and isinstance(slice, ast.Tuple):
                inners = tuple(
                    [self._get_type(scope, x, tmp_types) for x in slice.elts]
                )
                return Tuple[inners]
            if coll_type is Dict and isinstance(slice, ast.Tuple):
                key = self._get_type(scope, slice.elts[0], tmp_types)
                value = self._get_type(scope, slice.elts[1], tmp_types)
                return Dict[key, value]
            if coll_type is Callable and isinstance(slice, ast.Tuple):
                params = slice.elts[0]
                ret = self._get_type(scope, slice.elts[1], tmp_types)
                if isinstance(params, ast.List):
                    return Callable[
                        [self._get_type(scope, x, tmp_types) for x in params.elts], ret
                    ]
                else:
                    return Callable[..., ret]

            # handle an object which is a given standard type, e.g. `foo[5]` when `foo` is a List[int]
            c: Any = coll_type
            if _is_generic(List, c):
                # list slices return the same type
                if isinstance(slice, ast.Slice) and slice.upper is not None:
                    return c
                if not hasattr(c, "__args__") or c.__args__ is None:
                    return Any
                return c.__args__[0]
            if _is_generic(Optional, c):
                if not hasattr(c, "__args__") or c.__args__ is None:
                    return Any
                return c.__args__[0]
            if _is_generic(Dict, c):
                if not hasattr(c, "__args__") or c.__args__ is None:
                    return Any
                return c.__args__[1]
            if _is_generic(Tuple, c):
                # tuple slices return the same type
                if isinstance(slice, ast.Slice) and slice.upper is not None:
                    return c
                if hasattr(c, "__args__") and len(set(c.__args__)) == 1:
                    return c.__args__[0]
                if isinstance(slice, ast.Constant) and isinstance(slice.value, int):
                    i = slice.value
                    if hasattr(c, "__args__") and i < len(set(c.__args__)):
                        return c.__args__[i]

                # if the tuple isn't identically typed, we stop typing
                return self._type_failure(f"Ambiguous Tuple subscript {c}")

            # any other subscript, we don't attempt to generate type hints for
            return self._type_failure(f"Unknown subscripted type {c}")

        if isinstance(parsed, ast.Lambda):
            return Callable[..., Any]

        if isinstance(parsed, ast.FunctionDef):
            ret = None
            if parsed.returns is not None:
                ret = self._get_type(scope, parsed.returns, tmp_types)
            else:
                funcscope = self.scopes[parsed.lineno].ident
                if funcscope is not None:
                    call: Any = funcscope.type_obj
                    ret = call.__args__[-1]

            # don't generate args for 'complex' functions
            args = parsed.args
            if (
                args.kwarg is not None
                or args.vararg is not None
                or len(args.kwonlyargs) > 0
            ):
                return Callable[(..., ret)]

            if "posonlyargs" in args._fields and len(args.posonlyargs) > 0:
                return Callable[(..., ret)]

            # Callable isn't designed for methods, drop the self argument
            first = 0
            if isinstance(scope.parsed, ast.ClassDef) and args.args[0].arg == "self":
                first = 1

            arg_list = []
            for i in range(first, len(args.args)):
                annot = args.args[i].annotation
                if annot is None:
                    arg_list += [Any]
                else:
                    arg_list += [self._get_type(scope, annot, tmp_types)]

            return Callable[
                (
                    [a for a in arg_list],
                    ret,
                )
            ]

        if isinstance(parsed, ast.Constant):
            if parsed.value is None:
                return None
            return type(parsed.value)

        # for if expressions, assume that the types won't vary between each
        # branch and return the 'main' branch
        if isinstance(parsed, ast.IfExp):
            return self._get_type(scope, parsed.body, tmp_types)

        # for list comprehensions / generators we generate a tmp type for the iterator value
        if isinstance(parsed, ast.ListComp) or isinstance(parsed, ast.GeneratorExp):
            if tmp_types is None:
                extras = {}
            else:
                extras = tmp_types.copy()

            for g in parsed.generators:
                iter = self._get_type(scope, g.iter, tmp_types)

                if _is_generic(List, iter):
                    iter_type = iter.__args__[0]
                elif _is_generic(Tuple, iter):
                    if len(set(iter.__args__)) == 1:
                        iter_type = iter.__args__[0]
                    else:
                        return self._type_failure(
                            f"Ambiguouos tuple list comp on {iter}"
                        )
                else:
                    return self._type_failure(f"Unhandle iter list comp on {iter}")

                if isinstance(g.target, ast.Name):
                    extras[g.target.id] = iter_type
                elif (
                    isinstance(g.target, ast.Tuple)
                    and _is_generic(Tuple, iter_type)
                    and len(g.target.elts) == len(iter_type.__args__)
                ):
                    for i, e in enumerate(g.target.elts):
                        if not isinstance(e, ast.Name):
                            return self._type_failure(
                                f"Failed unpacking list comp {i} on {e}"
                            )
                        extras[e.id] = iter_type.__args__[i]

            inner = self._get_type(scope, parsed.elt, extras)
            if isinstance(inner, str):
                return self._type_failure(f"Failed Listcomp {inner}")
            return List[inner]

        # assume simple ops can be mostly type modeled as if they always return
        # the LHS type. Not true for int * float or int * str but close enough
        if isinstance(parsed, ast.BinOp):
            return self._get_type(scope, parsed.left, tmp_types)
        if isinstance(parsed, ast.UnaryOp):
            return self._get_type(scope, parsed.operand, tmp_types)
        # similarly, comparisons/bools don't consider overloads and just assume bool return
        if isinstance(parsed, ast.Compare) or isinstance(parsed, ast.BoolOp):
            return bool

        if sys.version_info >= (3, 14):
            if isinstance(parsed, ast.JoinedStr) or isinstance(parsed, ast.TemplateStr):
                return str

        # legacy types before consolidation into constant
        if sys.version_info < (3, 8):
            if isinstance(parsed, ast.Num):
                return type(parsed.n)
            if isinstance(parsed, ast.Str):
                return str
            if isinstance(parsed, ast.Bytes):
                return bytes
            if isinstance(parsed, ast.NameConstant):
                if parsed.value is None:
                    return None
                return type(parsed.value)

        return self._type_failure(f"General Type-lookup failure {parsed}")

    # we can try to guess function return values by looking at the types of
    # the return statements. If they are all the same (ignoring possible none)
    # then use that. If they're different we give up as we don't handle union/
    # varied types
    def _guess_return_value(self, scope: Scope, node: ast.AST):
        ret_types = [
            self._get_type(scope, r.value) for r in _get_return_statements(node)
        ]
        ret_types = list(set([r for r in ret_types if r is not None]))

        if len(ret_types) == 1 and not isinstance(ret_types[0], str):
            return ret_types[0]
        return type(None)

    # for a single statement, process the identifiers it creates and recurse
    # as needed (but not into new scopes)
    def _process_stmt(self, parent: Optional[Scope], parsed: ast.AST):
        if parent is None:
            raise ValueError("Expected parent for non-module")

        # for functions and classes we register their type as an identifier and update
        # scopes, but push them onto the pending list and continue processing.
        if isinstance(parsed, ast.ClassDef):
            classscope = Scope()
            classscope.name = f"class {parsed.name}"
            classscope.parent = parent
            classscope.parsed = parsed
            classscope.type_obj = TypeVar(parsed.name)  # type: ignore
            self.user_types[parsed.name] = classscope.type_obj
            classscope.is_class = True

            r = _get_linerange(parsed)

            for line in range(r[0], r[1] + 1):
                self.scopes[line] = classscope

            id = Ident()
            id.line = parsed.lineno
            id.type_obj = classscope.type_obj
            classscope.ident = id
            parent.set_ident(parsed.name, id)

            self.pending.append((classscope, parsed))

            return
        elif isinstance(parsed, ast.FunctionDef) or isinstance(
            parsed, ast.AsyncFunctionDef
        ):
            funcscope = Scope()
            funcscope.name = f"function {parsed.name}"
            funcscope.parent = parent
            funcscope.parsed = parsed
            funcscope.is_class = False

            r = _get_linerange(parsed)

            for line in range(r[0], r[1] + 1):
                self.scopes[line] = funcscope

            id = Ident()
            id.line = parsed.lineno
            id.type_obj = self._get_type(parent, parsed)
            # if there's no return annotation, we'll try to guess it
            # later when this function gets processed
            if parsed.returns is None:
                id.lazy_node = parsed
            else:
                # if we know the return type already and this is a @property
                # then pretend it is just a member of that type, not a function
                if any(
                    [
                        isinstance(a, ast.Name) and a.id == "property"
                        for a in parsed.decorator_list
                    ]
                ):
                    id.type_obj = self._get_type(parent, parsed.returns)

            prop_setter = False

            # if this one is a property setter, @self.setter, then
            # don't add it as an ident
            if any(
                [
                    isinstance(a, ast.Attribute)
                    and a.attr == "setter"
                    and isinstance(a.value, ast.Name)
                    and a.value.id == parsed.name
                    for a in parsed.decorator_list
                ]
            ):
                prop_setter = True

            funcscope.ident = id
            if not prop_setter:
                parent.set_ident(parsed.name, id)

            args = parsed.args
            arg_list = []
            if "posonlyargs" in args._fields:
                arg_list += args.posonlyargs
            arg_list += args.args

            # resize up the defaults array to the right size, defaults are 'trailing'
            # ie. if there are fewer defaults than arguments, the first ones (starting
            # from position-only arguments) have defaults omitted
            defaults = [None] * (len(arg_list) - len(args.defaults)) + args.defaults

            # len(kwonlyargs) == len(kw_defaults) because keyword defaults can come in
            # any order
            arg_list += args.kwonlyargs
            defaults += args.kw_defaults

            for i, a in enumerate(arg_list):
                id = Ident()
                id.line = parsed.lineno
                default_val = defaults[i]
                if a.annotation is not None:
                    id.type_obj = self._get_type(parent, a.annotation)
                elif default_val is not None:
                    id.type_obj = self._get_type(parent, default_val)
                elif (
                    a in parsed.args.args
                    and parsed.args.args.index(a) == 0
                    and a.arg == "self"
                    and parent.parent is not None
                ):
                    id.type_obj = parent.type_obj
                else:
                    if self.debug_types:
                        id.type_obj = self._type_failure(
                            f"Unknown parameter type {a.arg} in {parsed.name}"
                        )
                    id.type_obj = Any
                funcscope.set_ident(a.arg, id)

            self.pending.append((funcscope, parsed))

            return

        # for imports, try to import the module ourselves so that we can have proper types.
        # this won't work well for relative imports or things that need a particular sys.path
        # but will work for standard library modules
        if isinstance(parsed, ast.Import):
            for alias in parsed.names:
                n = alias.asname
                if n is None or n == "":
                    n = alias.name

                id = Ident()
                id.line = parsed.lineno
                try:
                    id.type_obj = __import__(alias.name, globals(), locals())
                except ImportError:
                    if self.debug_types:
                        print(f"Couldn't import {alias.name}")
                    id.type_obj = Any
                parent.set_ident(n, id)

        if isinstance(parsed, ast.ImportFrom):
            module = None
            if parsed.module is not None:
                try:
                    module = __import__(
                        parsed.module,
                        globals(),
                        locals(),
                        [a.name for a in parsed.names],
                        parsed.level,
                    )
                except ImportError:
                    if self.debug_types:
                        print(f"Couldn't import {parsed.module}")
                    module = None
            for alias in parsed.names:
                n = alias.asname
                if n is None or n == "":
                    n = alias.name

                id = Ident()
                id.line = parsed.lineno
                if module is None:
                    try:
                        id.type_obj = __import__(
                            alias.name, globals(), locals(), [], parsed.level
                        )
                    except ImportError:
                        if self.debug_types:
                            print(f"Couldn't import {alias.name}")
                        id.type_obj = Any
                else:
                    if hasattr(module, alias.name):
                        id.type_obj = getattr(module, alias.name)
                    else:
                        id.type_obj = Any
                parent.set_ident(n, id)

        # global/nonlocal we ignore for now, we assume the type won't
        # change with any assignments there

        targets = []
        values = []
        unpacking = False
        ident_col = None

        # AugAssign doesn't create a new object, so ignore it

        # for other things that create a new identifier register both the set of
        # target names and the source values. For pure assignments note the
        # column where the LHS ends so that we can identify both possibly different
        # types of `foo` in the statement `foo = foo.bar`

        if isinstance(parsed, ast.For) or isinstance(parsed, ast.AsyncFor):
            if isinstance(parsed.target, ast.Tuple) or isinstance(
                parsed.target, ast.List
            ):
                targets = parsed.target.elts
            else:
                targets = [parsed.target]
            values = [parsed.iter] * len(targets)
            unpacking = True

        # unless this actually creates a new name we don't have to do anything
        if isinstance(parsed, ast.With) or isinstance(parsed, ast.AsyncWith):
            for item in parsed.items:
                if item.optional_vars is not None:
                    if isinstance(item.optional_vars, ast.Name):
                        targets += [item.optional_vars]
                        values += [item.context_expr]
                    elif isinstance(item.optional_vars, ast.Tuple):
                        unpacking = True
                        targets += item.optional_vars.elts
                        values += [item.context_expr] * len(item.optional_vars.elts)

        # for annotated assignments, trust the annotation and don't try to evaluate the
        # actual RHS
        if isinstance(parsed, ast.AnnAssign):
            targets += [parsed.target]
            values += [parsed.annotation]
            if parsed.value is not None:
                ident_col = parsed.value.col_offset

        if isinstance(parsed, ast.Assign):
            if len(parsed.targets) == 1 and (
                isinstance(parsed.targets[0], ast.Tuple)
                or isinstance(parsed.targets[0], ast.List)
            ):
                targets = parsed.targets[0].elts
                unpacking = True
            else:
                targets = parsed.targets
            values = [parsed.value] * len(targets)
            if parsed.value is not None:
                ident_col = parsed.value.col_offset

        # starred has no effect for our purposes
        for i in range(len(targets)):
            t = targets[i]
            while isinstance(t, ast.Starred):
                t = t.value
            targets[i] = t

        # we were in control of these arrays so they should be identically sized
        if len(targets) != len(values):
            raise RuntimeError("Didn't get equal number of targets and values")

        for i in range(len(targets)):
            t = targets[i]

            ident_scope = parent
            if isinstance(t, ast.Name):
                name = t.id

                if isinstance(t.ctx, ast.Load):
                    raise ValueError("Didn't expect loading target")
            elif _is_self_lookup(t) and parent.parent is not None:
                # just to help the type checked, is_self_lookup already checked this
                if isinstance(t, ast.Attribute):
                    name = t.attr
                    ident_scope = parent.parent

                    # walk up to the class, in case of nested functions
                    while (
                        ident_scope.type_obj is None and ident_scope.parent is not None
                    ):
                        ident_scope = ident_scope.parent

                    if isinstance(t.ctx, ast.Load):
                        raise ValueError("Didn't expect loading target")
                else:
                    raise RuntimeError("invalid")
            else:
                # otherwise do nothing, this is an assignment of a value and we don't
                # track fully dynamic types and attributes
                continue

            id = Ident()
            id.line = _get_linerange(parsed)[0]
            v = values[i]
            id.type_obj = self._get_type(parent, v)

            if unpacking:
                t: Any = id.type_obj
                if _is_generic(Tuple, t):
                    # either out of bounds, or a `Tuple[...]`, either way call it Any
                    if i < len(t.__args__):
                        id.type_obj = t.__args__[i]
                    else:
                        id.type_obj = Any
                elif _is_generic(List, t):
                    id.type_obj = t.__args__[0]
                else:
                    id.type_obj = t

            if ident_col is not None:
                id.col = ident_col
            ident_scope.set_ident(name, id)

        # should only get here for things like loops, ifs, etc NOT for classes and functions
        if _is_scope_node(parsed):
            raise TypeError("Should not be recursing for scope node")

        for recurse in ["body", "orelse", "finalbody"]:
            if recurse in parsed._fields:
                for e in getattr(parsed, recurse):
                    self._process_stmt(parent, e)

    # function to process the n'th item in the pending list. Usually 0 to
    # continue processing in declaration order but can be out-of-order if we
    # want to crystallise a guessed return type for a function in order to
    # get a better type at an earlier callsite
    def _process_pending(self, idx: int):
        scope, node = self.pending.pop(idx)

        # node is a module, class, or function. Process all the identifiers in it
        # and add any nested classes or functions to the pending list
        if _is_scope_node(node):
            for st in getattr(node, "body"):
                self._process_stmt(scope, st)

            # for functions that want guessed return types (lazy_node is not None)
            # do that now
            if (
                scope is not None
                and scope.ident is not None
                and scope.ident.lazy_node is not None
            ):
                if scope.ident.type_obj is not None:
                    args = scope.ident.type_obj.__args__[0:-1]
                    ret_type = self._guess_return_value(scope, scope.ident.lazy_node)

                    # if this function was a property, don't make a callable just set
                    # the return type
                    if isinstance(scope.ident.lazy_node, ast.FunctionDef) and any(
                        [
                            isinstance(a, ast.Name) and a.id == "property"
                            for a in scope.ident.lazy_node.decorator_list
                        ]
                    ):
                        scope.ident.type_obj = ret_type
                    elif len(args) == 1 and args[0] == ...:
                        scope.ident.type_obj = Callable[(..., ret_type)]
                    else:
                        scope.ident.type_obj = Callable[[a for a in args], ret_type]
                scope.ident.lazy_node = None
                pass
        else:
            raise TypeError("Unexpected type of object in pending list")

    def _process_module(self):
        if self.module is not None:

            # start with just the module
            modscope = Scope()
            modscope.name = "module"
            modscope.parsed = self.module
            modscope.parent = None
            modscope.is_class = False

            # set all globals, ignoring reserved ones with __ prefix - so this can be
            # globals() without needing extra filtering
            for k, v in self.starting_globals.items():
                if k.startswith("__"):
                    continue
                id = Ident()
                id.line = 0
                id.type_obj = v
                modscope.set_ident(k, id)

            # modules don't have line ranges, so go to the last entry in the body
            if len(self.module.body) > 0:
                r = _get_linerange(self.module.body[-1])
            else:
                self.scopes = [modscope]
                return

            # start with every line pointing to the module scope
            self.scopes = [modscope] * (r[1] + 1)

            for e in self.module.body:
                self._process_stmt(modscope, e)

            while len(self.pending) > 0:
                self._process_pending(0)

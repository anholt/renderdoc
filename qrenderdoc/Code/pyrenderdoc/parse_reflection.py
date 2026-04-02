import ast
import builtins
from typing import List, Dict, Any, Tuple, Callable, TypeVar, Optional


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

        # try to parse the text
        self._parse_text(text)

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

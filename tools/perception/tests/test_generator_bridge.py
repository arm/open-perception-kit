################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib
import string
import sys
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/flowdata-sdk/tools/flowdata"))
bridge = importlib.import_module("engine.cpp.python_bridge")
engine_types = importlib.import_module("engine.types")


SCHEMA = r'''
namespace Demo;
enum State : uint8 { OFF = 0, ON = 1 }
struct Point { x:float; y:double; }
table Child { display_name:string; }
table Payload {
    enabled:bool = true;
    signed_value:int64;
    unsigned_value:uint32;
    score:float;
    state:State;
    label:string = "https://example.test/*literal*/";
    child:Child;
    point:Point;
    numbers:[short];
    labels:[string];
    states:[State];
    children:[Child];
    points:[Point];
}
root_type Payload;
'''


class GeneratorBridgeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.schema_path = Path("fixture.fbs")
        self.context = engine_types.GenerationContext(
            sdk_name="fixture",
            sdk_version=engine_types.SemanticVersion(1, 2, 3),
            schema_dir=Path("."),
            schema_paths=[self.schema_path],
            generated_root=Path("unused-output"),
            entrypoint_path=Path("unused-entrypoint"),
            flatc_bin="not-invoked",
            flatc_version=engine_types.SemanticVersion(25, 2, 10),
            flatc_version_output="not-invoked",
            tool_sources=[],
            cpp_python_bridge=True,
        )
        self.entry = engine_types.SchemaEntry(
            name="payload",
            namespace="Demo",
            root_type_name="Payload",
            qualified_root_type="Demo.Payload",
            table_type="Demo::Payload",
            native_type="Demo::PayloadT",
            create_fn="Demo::CreatePayload",
            schema_path=self.schema_path,
            schema_stem="fixture",
            numeric_id=1,
            file_identifier="TEST",
        )

    def parse(self, text: str) -> list[bridge.BridgeType]:
        with patch.object(Path, "read_text", return_value=text) as read_text:
            result = bridge._parse_schema_file(self.schema_path)
        read_text.assert_called_once_with(encoding="utf-8")
        return result

    def resolved_types(self, text: str = SCHEMA) -> dict[str, bridge.BridgeType]:
        with patch.object(Path, "read_text", return_value=text):
            return bridge._schema_types(self.context)

    def test_strip_comments_preserves_line_endings_and_token_adjacency(self) -> None:
        cases = [
            ("", ""),
            ("/", "/"),
            ("a/b", "a/b"),
            ("a//gone\r\nb//gone\rc//gone\nd", "a\r\nb\rc\nd"),
            ("a/*gone\r\n*/b/**/c", "abc"),
            ("a/* outer /* inner */b */", "ab */"),
            ("a// no newline", "a"),
            ("a/*", "a"),
            ("a/* unfinished *", "a"),
        ]
        for text, expected in cases:
            with self.subTest(text=text):
                self.assertEqual(bridge._strip_comments(text), expected)

    def test_strip_comments_preserves_escaped_strings(self) -> None:
        cases = [
            r'"https://host/*not a comment*/"',
            r'"escaped \" // still a string /* too */"',
            r'"escaped backslash \\"',
            '"unterminated // /*',
            '"trailing escape' + "\\",
        ]
        for text in cases:
            with self.subTest(text=text):
                self.assertEqual(bridge._strip_comments(text), text)
        self.assertEqual(bridge._strip_comments(r'"\\"/*gone*/next'), r'"\\"next')
        self.assertEqual(bridge._strip_comments('/* " // */x// "\ny'), "x\ny")

    def test_find_matching_brace_handles_nesting_offsets_and_escapes(self) -> None:
        for body in ("", "{nested {}}", r'"} { \" }" {inner}', r'"\\" {}'):
            text = "prefix{" + body + "}suffix{}"
            with self.subTest(body=body):
                self.assertEqual(bridge._find_matching_brace(text, 6), 7 + len(body))
        self.assertEqual(bridge._find_matching_brace("{}{}", 2), 3)

    def test_find_matching_brace_unterminated_inputs(self) -> None:
        for text in ("", "{", "{{}", '{"}"', '{"unfinished', '{"escape' + "\\"):
            with self.subTest(text=text):
                self.assertEqual(bridge._find_matching_brace(text, 0), -1)
        self.assertEqual(bridge._find_matching_brace("{}", 2), -1)

    def test_split_semicolons_handles_empty_parts_and_tail(self) -> None:
        for text, expected in (
            ("", []),
            (" \n", []),
            (";", [""]),
            (" a ; ; b; \n", ["a", "", "b"]),
            (" a ; tail ", ["a", "tail"]),
        ):
            with self.subTest(text=text):
                self.assertEqual(bridge._split_top_level_semicolons(text), expected)

    def test_split_semicolons_handles_independent_clamped_nesting(self) -> None:
        cases = [
            ("a:(x;(y;z));b:[x;[y;z]];c", ["a:(x;(y;z))", "b:[x;[y;z]]", "c"]),
            ("a:([x;y]);b", ["a:([x;y])", "b"]),
            (")];a;(unfinished;b", [")]", "a", "(unfinished;b"]),
            ("([)];tail", ["([)]", "tail"]),
            ("{a;b};c", ["{a", "b}", "c"]),
        ]
        for text, expected in cases:
            with self.subTest(text=text):
                self.assertEqual(bridge._split_top_level_semicolons(text), expected)

    def test_split_semicolons_preserves_string_and_escape_behavior(self) -> None:
        self.assertEqual(
            bridge._split_top_level_semicolons('a:"x;([)]///*";b'),
            ['a:"x;([)]///*"', "b"],
        )
        self.assertEqual(bridge._split_top_level_semicolons('a;"unfinished;b'), ["a", '"unfinished;b'])
        self.assertEqual(bridge._split_top_level_semicolons(r'a:"\\";b'), [r'a:"\\"', "b"])
        # The existing splitter counts every quote, including a backslash-escaped quote.
        self.assertEqual(bridge._split_top_level_semicolons(r'a:"x\";b";c'), [r'a:"x\"', 'b";c'])
        self.assertEqual(bridge._split_top_level_semicolons('"tail' + "\\"), ['"tail' + "\\"])

    def test_field_names_and_defaults(self) -> None:
        fields = bridge._parse_fields(
            'ignored; snake_case:uint32 = 7 (id: 0); _leading_:string (tag: "a;b"); '
            'alreadyCamel:[float]; repeated__word:bool;',
            self.schema_path, "Demo", "Payload",
        )
        self.assertEqual([field.py_name for field in fields], ["snakeCase", "Leading", "alreadyCamel", "repeatedWord"])
        self.assertEqual([field.cpp_name for field in fields], [field.schema_name for field in fields])
        self.assertEqual([field.type_ref.kind for field in fields], ["uint", "string", "vector", "bool"])

    def test_invalid_field_names_are_rejected_with_context(self) -> None:
        for name in ("", "9name", "bad-name", "two words", "caf\u00e9", "\u212a", "x\u0661"):
            with self.subTest(name=name), self.assertRaises(SystemExit) as raised:
                bridge._parse_fields(f"{name}:int;", self.schema_path, "Demo", "Payload")
            self.assertIn(f"field '{name}'", str(raised.exception))
            self.assertIn("fixture.fbs:Demo.Payload", str(raised.exception))

    def test_missing_field_types_are_rejected(self) -> None:
        for declaration in ("field:;", "field: = 1;", "field: (required);"):
            with self.subTest(declaration=declaration), self.assertRaises(SystemExit) as raised:
                bridge._parse_fields(declaration, self.schema_path, "Demo", "Payload")
            self.assertEqual(
                str(raised.exception),
                "cannot determine FlatBuffers field type for Demo.Payload.field in fixture.fbs",
            )

    def test_identifier_regex_keeps_ascii_semantics(self) -> None:
        characters = string.printable + "\u00e9\u212a\u0130\u017f\u0661\uff11\u0301\u00a0\u2003"
        for char in characters:
            with self.subTest(char=char):
                self.assertEqual(bool(bridge._CPP_IDENTIFIER_RE.fullmatch(char)), char in string.ascii_letters + "_")
                self.assertEqual(
                    bool(bridge._CPP_IDENTIFIER_RE.fullmatch("a" + char)),
                    char in string.ascii_letters + string.digits + "_",
                )

    def test_mangle_keeps_ascii_only_identifiers(self) -> None:
        for value, expected in (
            ("", "_"), ("9a", "_9a"), ("Demo.Payload", "Demo_Payload"),
            ("a-b/c:d", "a_b_c_d"), ("_valid9", "_valid9"),
            ("caf\u00e9", "caf_"), ("\u0661\u212a\U0001f600", "___"),
            ("e\u0301", "e_"), ("a\nb", "a_b"),
        ):
            with self.subTest(value=value):
                self.assertEqual(bridge._mangle(value), expected)

    def test_parse_scalar_aliases_and_recursive_vectors(self) -> None:
        aliases = {
            "int8": "byte", "uint8": "ubyte", "int16": "short", "uint16": "ushort",
            "int32": "int", "uint32": "uint", "int64": "long", "uint64": "ulong",
        }
        for name in ("bool", "byte", "ubyte", "short", "ushort", "int", "uint", "long", "ulong", "float", "double", "string"):
            aliases[name] = name
        for name, kind in aliases.items():
            with self.subTest(name=name):
                self.assertEqual(bridge._parse_type_ref(f" {name} "), bridge.BridgeTypeRef(kind, name))
        nested = bridge._parse_type_ref("[ [ Demo.Point ] ]")
        self.assertEqual(nested.kind, "vector")
        self.assertEqual(nested.element.kind, "vector")
        self.assertEqual(nested.element.element, bridge.BridgeTypeRef("named", "Demo.Point"))
        for name in ("Missing", "[int:3]", "[int", "[]"):
            with self.subTest(name=name):
                self.assertEqual(bridge._parse_type_ref(name).name, name)

    def test_schema_declarations_and_namespace_changes(self) -> None:
        types = self.parse(
            'table Global {} namespace A.B; struct Point {x:float;} '
            'enum Mode : uint8 {OFF, ON} namespace Other; table Next {point:A.B.Point;} '
            'namespace ; enum Default {ZERO}'
        )
        self.assertEqual([item.qualified_name for item in types], [
                         "Global", "A.B.Point", "A.B.Mode", "Other.Next", "Default"])
        self.assertEqual([item.cpp_type for item in types], ["GlobalT",
                         "A::B::Point", "A::B::Mode", "Other::NextT", "Default"])
        self.assertEqual([item.py_type_name for item in types], ["GlobalT", "Point", "Mode", "NextT", "Default"])
        self.assertEqual(types[2].enum_underlying, "uint8")
        self.assertEqual(types[4].enum_underlying, "int")

    def test_schema_comments_strings_and_nested_braces(self) -> None:
        types = self.parse(r'''
            // table Wrong {}
            namespace Demo;
            /* struct Wrong {} */
            table Payload {
                label:string = "escaped \" // /* { }";
            }
            enum State { NESTED = { value } }
            table End {}
        ''')
        self.assertEqual([item.name for item in types], ["Payload", "State", "End"])
        self.assertEqual(types[0].fields[0].type_ref.kind, "string")

    def test_schema_declaration_whitespace_remains_unicode(self) -> None:
        for whitespace in (" ", "\t", "\n", "\u00a0", "\u2003", "\u2028"):
            with self.subTest(whitespace=whitespace):
                self.assertEqual(self.parse(f"table{whitespace}_Name9 {{}}")[0].name, "_Name9")
        for name in ("\u00e9", "\u212a", "\u0661", "9bad"):
            with self.subTest(name=name), self.assertRaisesRegex(SystemExit, "missing table name"):
                self.parse(f"table {name} {{}}")
        # Declaration matching consumes an ASCII prefix, unlike full field-name validation.
        self.assertEqual(self.parse("table caf\u00e9 {}")[0].name, "caf")

    def test_schema_token_word_boundaries_remain_unicode(self) -> None:
        self.assertEqual(self.parse("\u00e9table X {} table\u00e9 Y {} notable Z {}"), [])
        self.assertEqual(self.parse("// only comments\n/* nothing */"), [])

    def test_namespace_error(self) -> None:
        with self.assertRaisesRegex(SystemExit, "^unterminated namespace declaration in fixture.fbs$"):
            self.parse("namespace Demo")

    def test_table_struct_and_enum_declaration_errors(self) -> None:
        for kind in ("table", "struct", "enum"):
            cases = (
                (f"{kind} {{}}", f"missing {kind} name in fixture.fbs"),
                (f"{kind} Item", f"missing '{{' for {kind} Demo.Item in fixture.fbs"),
                (f"{kind} Item {{", f"unterminated {kind} Demo.Item in fixture.fbs"),
                (f'{kind} Item {{ "}}"', f"unterminated {kind} Demo.Item in fixture.fbs"),
            )
            for text, expected in cases:
                with self.subTest(kind=kind, text=text), self.assertRaises(SystemExit) as raised:
                    self.parse("namespace Demo; " + text)
                self.assertEqual(str(raised.exception), expected)

    def test_enum_underlying_type_validation_and_existing_empty_header_error(self) -> None:
        for underlying in ("string", "Missing", "[int]"):
            with self.subTest(underlying=underlying), self.assertRaises(SystemExit) as raised:
                self.parse(f"namespace Demo; enum State : {underlying} {{ZERO}}")
            self.assertEqual(
                str(raised.exception),
                f"cannot generate C++ Python bridge for enum Demo.State: unsupported underlying type '{underlying}'",
            )
        for underlying in bridge._SCALAR_TYPES:
            with self.subTest(underlying=underlying):
                self.assertEqual(self.parse(f"enum State : {underlying} (attr) {{ZERO}}")[
                                 0].enum_underlying, underlying)
        with self.assertRaises(IndexError):
            self.parse("enum State : {}")

    def test_schema_read_errors_propagate(self) -> None:
        with patch.object(Path, "read_text", side_effect=OSError("unreadable")):
            with self.assertRaisesRegex(OSError, "unreadable"):
                bridge._parse_schema_file(self.schema_path)

    def test_namespace_resolution_across_schema_files(self) -> None:
        context = replace(self.context, schema_paths=[Path("one.fbs"), Path("two.fbs")])
        with patch.object(Path, "read_text", side_effect=[
            "namespace A; table Payload {points:[B.Point]; state:State;}",
            "namespace B; struct Point {x:float;} enum State:byte {ZERO}",
        ]):
            types = bridge._schema_types(context)
        fields = types["A.Payload"].fields
        self.assertEqual(fields[0].type_ref.element, bridge.BridgeTypeRef("struct", "B.Point", "B.Point"))
        self.assertEqual(fields[1].type_ref, bridge.BridgeTypeRef("enum", "State", "B.State"))

    def test_named_resolution_qualification_global_and_ambiguity(self) -> None:
        types = {item.qualified_name: item for item in self.parse(
            "table Global {} namespace A; table Local {} namespace B; table Remote {}"
        )}
        for name, namespace, expected in (
            ("Global", "A", "Global"), ("Local", "A", "A.Local"),
            ("Remote", "A", "B.Remote"), ("B.Remote", "A", "B.Remote"),
        ):
            with self.subTest(name=name):
                self.assertEqual(bridge._resolve_named_type(name, namespace, types), expected)
        ambiguous = {item.qualified_name: item for item in self.parse(
            "table Item {} namespace A; table Item {} namespace B; table Item {}"
        )}
        with self.assertRaises(SystemExit) as raised:
            bridge._resolve_named_type("Item", "A", ambiguous)
        self.assertIn("A.Item, Item, B.Item", str(raised.exception))
        self.assertEqual(bridge._resolve_named_type("A.Item", "B", ambiguous), "A.Item")
        with self.assertRaisesRegex(SystemExit, "cannot resolve.*Missing.Item"):
            bridge._resolve_named_type("Missing.Item", "A", types)

    def test_duplicate_and_unsupported_schema_types(self) -> None:
        with self.assertRaisesRegex(SystemExit, "duplicate FlatBuffers type A.Item"):
            self.resolved_types("namespace A; table Item {} struct Item {}")
        for type_name in ("Missing", "[Missing]", "[int:3]", "[]"):
            with self.subTest(type_name=type_name), self.assertRaisesRegex(SystemExit, "cannot resolve FlatBuffers type reference"):
                self.resolved_types(f"table Payload {{field:{type_name};}}")
        with self.assertRaisesRegex(SystemExit, "vector field without element type"):
            bridge._resolve_type_ref(bridge.BridgeTypeRef("vector", "[]"), "", {})
        scalar = bridge.BridgeTypeRef("int", "int32")
        self.assertIs(bridge._resolve_type_ref(scalar, "", {}), scalar)

    def test_cpp_scalar_and_vector_types(self) -> None:
        expected = {
            "bool": "bool", "byte": "std::int8_t", "ubyte": "std::uint8_t",
            "short": "std::int16_t", "ushort": "std::uint16_t", "int": "std::int32_t",
            "uint": "std::uint32_t", "long": "std::int64_t", "ulong": "std::uint64_t",
            "float": "float", "double": "double", "string": "std::string",
        }
        for kind, cpp_type in expected.items():
            with self.subTest(kind=kind):
                ref = bridge.BridgeTypeRef(kind, kind)
                self.assertEqual(bridge._ref_cpp_type(ref, {}), cpp_type)
                self.assertEqual(bridge._vector_cpp_type(bridge.BridgeTypeRef(
                    "vector", "[]", element=ref), {}), f"std::vector<{cpp_type}>")
        types = self.resolved_types()
        fields = {field.schema_name: field for field in types["Demo.Payload"].fields}
        for name, cpp_type in (
            ("children", "std::vector<std::unique_ptr<Demo::ChildT>>"),
            ("points", "std::vector<Demo::Point>"),
            ("states", "std::vector<Demo::State>"),
        ):
            with self.subTest(name=name):
                self.assertEqual(bridge._vector_cpp_type(fields[name].type_ref, types), cpp_type)

    def test_unsupported_bridge_type_errors(self) -> None:
        unknown = bridge.BridgeTypeRef("unknown", "Unknown")
        for ref in (unknown, bridge.BridgeTypeRef("vector", "[]")):
            with self.subTest(ref=ref), self.assertRaisesRegex(SystemExit, r"vector C\+\+ type requested for non-vector field"):
                bridge._vector_cpp_type(ref, {})
        with self.assertRaisesRegex(SystemExit, "unsupported bridge type reference"):
            bridge._ref_cpp_type(unknown, {})
        with self.assertRaisesRegex(SystemExit, "unsupported scalar bridge type unknown"):
            bridge._return_scalar_code(unknown, "value")
        with self.assertRaisesRegex(SystemExit, "unsupported field type unknown"):
            bridge._return_value_code(unknown, "value", {}, "anchor")
        with self.assertRaisesRegex(SystemExit, "vector item requested without element type"):
            bridge._return_vector_item_code(bridge.BridgeTypeRef("vector", "[]"), {}, "anchor")
        nested = bridge._parse_type_ref("[[int]]")
        with self.assertRaisesRegex(SystemExit, "unsupported bridge type reference"):
            bridge._vector_cpp_type(nested, {})

    def test_proxy_names_and_field_access(self) -> None:
        types = self.resolved_types()
        point = types["Demo.Point"]
        child = types["Demo.Child"]
        self.assertEqual(bridge._field_access(point, point.fields[0]), "value->x()")
        self.assertEqual(bridge._field_access(child, child.fields[0]), "value->display_name")
        self.assertEqual(bridge._field_getter_name(child, child.fields[0]), "get_Demo_Child_display_name")
        self.assertEqual(bridge._python_fb_module("fixture", child), "fixture.fb.Demo.Child")
        global_type = self.parse("table Global {}")[0]
        self.assertEqual(bridge._python_fb_module("fixture", global_type), "fixture.fb.Global")

    def test_emitted_bridge_structures_without_writing_files(self) -> None:
        with patch.object(Path, "read_text", return_value=SCHEMA), patch.object(Path, "mkdir") as mkdir, patch.object(Path, "write_text") as write_text:
            paths = bridge.generate_python_bridge(self.context, [self.entry])
        directory = self.context.cpp_root / "python_bridge"
        self.assertEqual(paths, [directory / "fixture_python_bridge.h", directory / "fixture_python_bridge.cpp"])
        mkdir.assert_called_once_with(parents=True, exist_ok=True)
        self.assertEqual(write_text.call_count, 2)
        header, source = [item.args[0] for item in write_text.call_args_list]
        self.assertEqual([item.kwargs for item in write_text.call_args_list], [{"encoding": "utf-8"}] * 2)
        for snippet in ('#include "fixture.h"', "namespace fixture::python_bridge", "class scoped_envelope"):
            self.assertIn(snippet, header)
        snippets = (
            '#include "fixture_python_bridge.h"', "kind_Demo_Payload", '"fixture.fb.Demo.Payload", "PayloadT"',
            'add_known_payload<Demo::PayloadT>(*live->envelope, value, "TEST")',
            "std::vector<std::unique_ptr<Demo::ChildT>>", "std::vector<Demo::Point>",
            "std::vector<Demo::State>", "std::vector<std::string>", "std::vector<std::int16_t>",
            '"displayName"', '"read-only display_name"', "return py_long_from_signed(value->signed_value);",
            "return py_long_from_unsigned(value->unsigned_value);", "return py_long_from_enum(value->state);",
            "return PyFloat_FromDouble(static_cast<double>(value->score));", "if (value->enabled)",
            "const auto* nested = value->child.get();", "const auto* nested = value->point.get();",
            "make_Demo_Child_proxy(item.get(),", "make_Demo_Point_proxy(std::addressof(item),",
            "proxy_anchor<Demo::PayloadT>(self)", "return py_string_from_std(value->label);",
            "sequence.sq_item = item_Demo_Payload_points_vector;", 'type.tp_name = "fixture_bridge.Demo_Payload";',
            "ensure_known_proxy_types()", "type.tp_new = nullptr;", "PyExc_IndexError",
        )
        for snippet in snippets:
            with self.subTest(snippet=snippet):
                self.assertIn(snippet, source)
        self.assertNotIn("pytype_Demo_State()", source)
        for token in ("__SDK_NAME__", "__MODULE_NAME__", "__KNOWN_PROXY", "__KNOWN_PAYLOAD", "__BRIDGE_HEADER__"):
            self.assertNotIn(token, source)

    def test_root_payload_validation_does_not_write_files(self) -> None:
        for schema, message in (
            ("table Other {}", "root payload type Demo.Payload was not found"),
            ("namespace Demo; struct Payload {}", "root payloads must be FlatBuffers tables"),
            ("namespace Demo; enum Payload {ZERO}", "root payloads must be FlatBuffers tables"),
        ):
            with self.subTest(schema=schema), patch.object(Path, "read_text", return_value=schema), patch.object(Path, "mkdir") as mkdir, patch.object(Path, "write_text") as write_text:
                with self.assertRaisesRegex(SystemExit, message):
                    bridge.generate_python_bridge(self.context, [self.entry])
                mkdir.assert_not_called()
                write_text.assert_not_called()

    def test_empty_proxy_generation_and_render_replacement_order(self) -> None:
        self.assertEqual(bridge._generate_proxy_declarations([], []), "")
        definitions = bridge._generate_proxy_definitions({}, [])
        self.assertIn("bool ensure_known_proxy_types() {\n    return true;", definitions)
        self.assertIn("(void)python_module;", definitions)
        self.assertEqual(
            bridge._render("__CUSTOM__ __SDK_HEADER__ __BRIDGE_HEADER__",
                           self.context, {"__CUSTOM__": "__MODULE_NAME__"}),
            "fixture_bridge fixture.h fixture_python_bridge.h",
        )


if __name__ == "__main__":
    unittest.main()

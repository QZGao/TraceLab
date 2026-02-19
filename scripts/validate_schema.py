#!/usr/bin/env python3
import argparse
import json
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a JSON instance against a JSON Schema.")
    parser.add_argument("--schema", required=True, help="Path to JSON schema")
    parser.add_argument("--instance", required=True, help="Path to JSON instance")
    args = parser.parse_args()

    try:
        import jsonschema  # type: ignore
    except Exception as exc:
        print("jsonschema package is required for validation.", file=sys.stderr)
        print(f"Import error: {exc}", file=sys.stderr)
        return 2

    with open(args.schema, "r", encoding="utf-8") as f:
        schema = json.load(f)
    with open(args.instance, "r", encoding="utf-8") as f:
        instance = json.load(f)

    try:
        jsonschema.validate(instance=instance, schema=schema)  # type: ignore
    except Exception as exc:
        print("schema validation failed", file=sys.stderr)
        print(str(exc), file=sys.stderr)
        return 1

    print("schema validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

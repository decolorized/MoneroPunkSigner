#!/usr/bin/env python3
"""
prepare_fork.py — находит изменения относительно апстрима Feather
и переносит их в новый форк.

Что делает:
  1. Проверяет, что исходный репозиторий — это Git-репозиторий Feather.
  2. Находит апстрим (origin/master) и все ваши коммиты поверх него.
  3. Выводит список изменённых/новых/удалённых файлов.
  4. Копирует только эти файлы в целевой форк (если он указан).
  5. Показывает, что осталось сделать (git add, commit, push).

Использование:
  # Только посмотреть, что изменено
  python3 prepare_fork.py ~/feather

  # Скопировать изменения в форк
  python3 prepare_fork.py ~/feather ~/MoneroPunkSigner

  # Сравнить с конкретной веткой/коммитом (по умолчанию origin/master)
  python3 prepare_fork.py ~/feather ~/MoneroPunkSigner --base origin/master
"""

import argparse
import os
import shutil
import subprocess
import sys


def run_git(repo: str, *args: str) -> str:
    """Запускает git-команду в указанном репозитории и возвращает stdout."""
    try:
        result = subprocess.run(
            ["git", "-C", repo, *args],
            capture_output=True, text=True, check=True,
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError as e:
        print(f"  [git error] git -C {repo} {' '.join(args)}", file=sys.stderr)
        print(f"  {e.stderr.strip()}", file=sys.stderr)
        return ""


def is_git_repo(path: str) -> bool:
    """Проверяет, является ли path Git-репозиторием."""
    if not os.path.isdir(path):
        return False
    return os.path.isdir(os.path.join(path, ".git"))


def get_changed_files(repo: str, base: str) -> dict:
    """
    Возвращает словарь с тремя списками:
      - modified:  файлы, которые есть и в base, и в HEAD, но изменены
      - added:     файлы, которых нет в base, но есть в HEAD
      - deleted:   файлы, которые есть в base, но удалены в HEAD
    """
    # --diff-filter=M — modified, A — added, D — deleted
    raw = run_git(repo, "diff", "--name-status", f"{base}..HEAD")
    modified, added, deleted = [], [], []

    for line in raw.splitlines():
        parts = line.split("\t", 1)
        if len(parts) != 2:
            continue
        status, path = parts[0], parts[1]
        if status.startswith("M"):
            modified.append(path)
        elif status.startswith("A"):
            added.append(path)
        elif status.startswith("D"):
            deleted.append(path)
        elif status.startswith("R"):
            # Переименование: R100\told\tnew
            old, new = path.split("\t", 1)
            modified.append(new)
            deleted.append(old)

    return {"modified": modified, "added": added, "deleted": deleted}


def get_commits(repo: str, base: str) -> list:
    """Возвращает список коммитов поверх base."""
    raw = run_git(repo, "log", "--oneline", f"{base}..HEAD")
    return raw.splitlines() if raw else []


def copy_file(src_root: str, dst_root: str, rel_path: str) -> bool:
    """Копирует один файл из src_root в dst_root, сохраняя структуру."""
    src = os.path.join(src_root, rel_path)
    dst = os.path.join(dst_root, rel_path)

    if not os.path.exists(src):
        print(f"  [skip] {rel_path}: нет в источнике")
        return False

    os.makedirs(os.path.dirname(dst), exist_ok=True)

    try:
        shutil.copy2(src, dst)
        return True
    except OSError as e:
        print(f"  [error] {rel_path}: {e}", file=sys.stderr)
        return False


def delete_file(dst_root: str, rel_path: str) -> bool:
    """Удаляет файл в dst_root, если он существует."""
    dst = os.path.join(dst_root, rel_path)
    if os.path.exists(dst):
        try:
            os.remove(dst)
            return True
        except OSError as e:
            print(f"  [error] {rel_path}: {e}", file=sys.stderr)
            return False
    return False


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Находит изменения относительно апстрима Feather и переносит их в форк."
    )
    parser.add_argument("source", help="Путь к вашему репозиторию Feather")
    parser.add_argument(
        "target", nargs="?",
        help="Путь к целевому форку (если не указан — только показать изменения)"
    )
    parser.add_argument(
        "--base", default="origin/master",
        help="База для сравнения (по умолчанию origin/master)"
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Только показать, что будет скопировано, не трогая файлы"
    )
    args = parser.parse_args()

    src = os.path.abspath(os.path.expanduser(args.source))
    dst = os.path.abspath(os.path.expanduser(args.target)) if args.target else None

    # --- 1. Проверки ---
    if not is_git_repo(src):
        print(f"Ошибка: {src} — не Git-репозиторий", file=sys.stderr)
        return 1

    if dst and not is_git_repo(dst):
        print(f"Ошибка: {dst} — не Git-репозиторий", file=sys.stderr)
        return 1

    print(f"Источник: {src}")
    print(f"Цель:     {dst if dst else '(только показать)'}")
    print(f"База:     {args.base}")
    print()

    # --- 2. Проверяем, что база существует ---
    if not run_git(src, "rev-parse", "--verify", args.base):
        print(f"Ошибка: база '{args.base}' не найдена в {src}", file=sys.stderr)
        print("Проверьте: git remote -v и git branch -a", file=sys.stderr)
        return 1

    # --- 3. Собираем информацию ---
    commits = get_commits(src, args.base)
    changes = get_changed_files(src, args.base)

    print("=" * 70)
    print(f"ВАШИ КОММИТЫ поверх {args.base}:")
    print("=" * 70)
    if commits:
        for c in commits:
            print(f"  {c}")
    else:
        print("  (нет коммитов — изменения только в рабочей директории?)")
    print()

    print("=" * 70)
    print("ИЗМЕНЁННЫЕ ФАЙЛЫ:")
    print("=" * 70)
    print(f"  modified: {len(changes['modified'])}")
    for f in changes["modified"]:
        print(f"    M  {f}")
    print(f"  added:    {len(changes['added'])}")
    for f in changes["added"]:
        print(f"    A  {f}")
    print(f"  deleted:  {len(changes['deleted'])}")
    for f in changes["deleted"]:
        print(f"    D  {f}")
    print()

    total = len(changes["modified"]) + len(changes["added"])
    if total == 0 and not changes["deleted"]:
        print("Изменений относительно базы не найдено.")
        print("Если вы правили файлы, но не коммитили — сделайте:")
        print("  git add -A && git commit -m '...'")
        return 0

    # --- 4. Если target не указан — только показываем ---
    if not dst:
        print("Целевой форк не указан — только показали изменения.")
        print(f"Чтобы скопировать, запустите:")
        print(f"  python3 {sys.argv[0]} {args.source} <путь-к-форку>")
        return 0

    # --- 5. Копируем ---
    print("=" * 70)
    print("КОПИРОВАНИЕ В ФОРК:")
    print("=" * 70)

    if args.dry_run:
        print("(dry-run: файлы не будут изменены)")
        print()

    copied, skipped, deleted = 0, 0, 0

    for f in changes["modified"] + changes["added"]:
        if args.dry_run:
            print(f"  [would copy] {f}")
            copied += 1
        else:
            if copy_file(src, dst, f):
                print(f"  [copied] {f}")
                copied += 1
            else:
                skipped += 1

    for f in changes["deleted"]:
        if args.dry_run:
            print(f"  [would delete] {f}")
            deleted += 1
        else:
            if delete_file(dst, f):
                print(f"  [deleted] {f}")
                deleted += 1

    print()
    print(f"Скопировано:  {copied}")
    print(f"Удалено:      {deleted}")
    print(f"Пропущено:    {skipped}")
    print()

    # --- 6. Что делать дальше ---
    print("=" * 70)
    print("ЧТО ДЕЛАТЬ ДАЛЬШЕ:")
    print("=" * 70)
    print(f"  cd {dst}")
    print("  git status")
    print("  git add -A")
    print("  git commit -m \"Add HID support for MoneroPunkSigner cold wallet\"")
    print("  git push origin master")
    print()

    return 0


if __name__ == "__main__":
    sys.exit(main())
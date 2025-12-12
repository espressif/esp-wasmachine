#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
#

"""
Apply a unified diff patch to a target directory using the python 'patch' package.
Supports both standard unified diff and git format-patch formats.
Usage:
    python3 apply_patch.py --patch <patch_file> --target <target_dir>
    python3 apply_patch.py -p <patch_file> -t <target_dir>
"""
import os
import sys
import argparse
import tempfile
import re

try:
    import patch
except ImportError:
    print("Error: Python 'patch' package is not installed.")
    print('Please install it using: pip install patch')
    sys.exit(1)

def is_git_format_patch(patch_file):
    """Check if the patch file is in git format-patch format (with email headers)."""
    try:
        with open(patch_file, 'r', encoding='utf-8', errors='ignore') as f:
            first_line = f.readline()
            return first_line.startswith('From ') and 'Mon Sep 17' in first_line
    except Exception:
        return False

def extract_new_files_from_patch(patch_file):
    """Extract new files and their content from patch file."""
    new_files = {}
    
    with open(patch_file, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    # Find all new file sections using regex
    # Pattern: --- /dev/null followed by +++ [b/]path and content
    # The 'b/' prefix is optional to support both git-style and plain unified diffs
    pattern = r'---\s+/dev/null\s*\n\+\+\+\s+(?:b/)?(.+?)\s*\n@@.*?\n((?:^\+.*\n?)+)'
    
    matches = re.finditer(pattern, content, re.MULTILINE)
    
    for match in matches:
        file_path = match.group(1)
        # Extract all lines starting with +
        content_lines = match.group(2).split('\n')
        # Remove leading + from each line
        file_content = '\n'.join(line[1:] if line.startswith('+') else line for line in content_lines if line)
        # Ensure file ends with newline
        if file_content and not file_content.endswith('\n'):
            file_content += '\n'
        new_files[file_path] = file_content
    
    return new_files

def extract_diff_excluding_new_files(patch_file, exclude_new_files=None, verbose=False):
    """Extract diff excluding new files from patch file (works for both git diff and git format-patch)."""
    if exclude_new_files is None:
        exclude_new_files = set()
    
    diff_lines = []
    excluded_count = 0
    excluded_files = []
    in_excluded_section = False
    
    with open(patch_file, 'r', encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()
    
    i = 0
    while i < len(lines):
        line = lines[i]
        
        if line.startswith('diff --git'):
            # Check if this is a new file we want to exclude
            match = re.search(r'diff --git a/(.+?) b/(.+?)$', line)
            if match:
                file_path = match.group(2)  # b/ path is the target file
                if file_path in exclude_new_files:
                    # Skip this entire diff block until next 'diff --git'
                    excluded_count += 1
                    excluded_files.append(file_path)
                    if verbose:
                        print(f'  Excluding diff block for: {file_path}')
                    in_excluded_section = True
                    i += 1
                    # Skip all lines until the next 'diff --git' or end of file
                    while i < len(lines):
                        if lines[i].startswith('diff --git'):
                            # Found next diff, stop skipping
                            break
                        i += 1
                    # Don't increment i here, let the loop handle the next 'diff --git'
                    in_excluded_section = False
                    continue
            
            # Not excluded, add this line and continue
            in_excluded_section = False
            diff_lines.append(line)
            i += 1
        elif in_excluded_section:
            # Skip lines in excluded section (shouldn't reach here, but just in case)
            i += 1
        else:
            # Normal line, add it
            diff_lines.append(line)
            i += 1
    
    if not diff_lines:
        return None, excluded_count, excluded_files
    
    # Create a temporary file with the pure diff
    temp_fd, temp_path = tempfile.mkstemp(suffix='.patch', text=True)
    try:
        try:
            f = os.fdopen(temp_fd, 'w')
        except Exception:
            os.close(temp_fd)
            raise
        try:
            with f:
                f.writelines(diff_lines)
            return temp_path, excluded_count, excluded_files
        except Exception:
            if os.path.exists(temp_path):
                os.unlink(temp_path)
            return None, excluded_count, excluded_files
    except Exception:
        if os.path.exists(temp_path):
            os.unlink(temp_path)
        return None, excluded_count, excluded_files

def detect_strip_level(patch_file, specified_strip_level=None, verbose=False):
    """Detect strip level from patch file or use specified value.
    
    Returns the strip level to use (0 or 1).
    If specified_strip_level is provided, uses that.
    Otherwise, auto-detects by checking for 'a/' and 'b/' prefixes in the patch file.
    """
    if specified_strip_level is not None:
        if verbose:
            print(f'Using specified strip level: {specified_strip_level}')
        return specified_strip_level
    
    # Auto-detect by checking for git prefixes in the patch file
    has_git_prefixes = False
    try:
        with open(patch_file, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                # Check for 'diff --git a/... b/...' format
                if line.startswith('diff --git'):
                    if ' a/' in line and ' b/' in line:
                        has_git_prefixes = True
                        break
                # Check for '--- a/...' or '+++ b/...' format
                elif line.startswith('--- ') or line.startswith('+++ '):
                    if line.startswith('--- a/') or line.startswith('+++ b/'):
                        has_git_prefixes = True
                        break
    except Exception:
        # If we can't read the file, default to 0
        has_git_prefixes = False
    
    strip_level = 1 if has_git_prefixes else 0
    if verbose:
        print(f'Auto-detected strip level: {strip_level} (has_git_prefixes={has_git_prefixes})')
    return strip_level

def create_new_files(new_files, target_dir, strip_level=1, verbose=False):
    """Create new files that should have been created by the patch."""
    created_count = 0
    for file_path, content in new_files.items():
        # Apply strip level
        if strip_level > 0:
            # Remove 'b/' prefix if present
            if file_path.startswith('b/'):
                file_path = file_path[2:]
            # Remove 'a/' prefix if present
            elif file_path.startswith('a/'):
                file_path = file_path[2:]
        
        # Normalize and resolve paths to prevent path-traversal attacks
        full_path = os.path.join(target_dir, file_path)
        full_path = os.path.normpath(full_path)
        
        # Validate that the resolved path is within target_dir (allow equality)
        target_abs = os.path.abspath(target_dir)
        full_abs = os.path.abspath(full_path)
        try:
            common = os.path.commonpath([full_abs, target_abs])
            if common != target_abs:
                print(f'  Security warning: Skipping file with path traversal: {file_path}')
                continue
        except ValueError:
            print(f'  Security warning: Skipping file with path traversal: {file_path}')
            continue
        
        # Check if file already exists
        if os.path.exists(full_path):
            if verbose:
                print(f'  New file already exists: {file_path}')
            continue
        
        # Create directory if needed
        file_dir = os.path.dirname(full_path)
        if file_dir and not os.path.exists(file_dir):
            os.makedirs(file_dir, exist_ok=True)
            if verbose:
                print(f'  Created directory: {file_dir}')
        
        # Create the file
        try:
            with open(full_path, 'w', encoding='utf-8') as f:
                f.write(content)
            if verbose:
                print(f'  Created new file: {file_path}')
            created_count += 1
        except Exception as e:
            print(f'  Error creating file {file_path}: {e}')
    
    return created_count

def to_str(value):
    """Convert bytes or str to str."""
    if isinstance(value, bytes):
        return value.decode('utf-8', errors='ignore')
    return str(value) if value is not None else None

def main():
    parser = argparse.ArgumentParser(
        description='Apply a unified diff patch to a target directory using python-patch',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 apply_patch.py --patch tools/patch/lvgl.patch --target managed_components/lvgl__lvgl
  python3 apply_patch.py -p my.patch -t my_dir
        """
    )
    parser.add_argument('--patch', '-p', required=True, help='Path to the patch file to apply')
    parser.add_argument('--target', '-t', required=True, help='Target directory to apply the patch to')
    parser.add_argument('--strip', '-s', type=int, default=None, help='Number of path components to strip (auto-detect if not specified)')
    parser.add_argument('--verbose', '-v', action='store_true', help='Enable verbose output')
    parser.add_argument('--debug', '-d', action='store_true', help='Enable debug output (saves temp patch file)')
    args = parser.parse_args()

    patch_file = os.path.abspath(args.patch)
    target_dir = os.path.abspath(args.target)

    if not os.path.isfile(patch_file):
        print(f"Error: Patch file '{patch_file}' does not exist.")
        sys.exit(1)
    if not os.path.isdir(target_dir):
        print(f"Error: Target directory '{target_dir}' does not exist.")
        sys.exit(1)

    # Extract new files from the original patch file first
    if args.verbose:
        print('Extracting new files from patch...')
    new_files = extract_new_files_from_patch(patch_file)
    if args.verbose and new_files:
        print(f'Found {len(new_files)} new file(s) in patch:')
        for file_path in new_files.keys():
            print(f'  - {file_path}')

    # Check if it's a git format-patch file (with email headers)
    is_git_patch = is_git_format_patch(patch_file)
    if args.verbose:
        print(f'Is git format-patch (with email headers): {is_git_patch}')
    
    actual_patch_file = patch_file
    temp_patch_file = None
    
    # Always exclude new files from the patch, regardless of format
    if new_files:
        if args.verbose:
            print('Excluding new files from patch...')
        # Exclude new files from the patch
        exclude_files = set(new_files.keys())
        if args.verbose:
            print(f'Files to exclude: {exclude_files}')
        temp_patch_file, excluded_count, excluded_files = extract_diff_excluding_new_files(patch_file, exclude_files, args.verbose)
        actual_patch_file = temp_patch_file
        if temp_patch_file:
            if args.verbose:
                print(f'Extracted diff to temporary file: {temp_patch_file}')
                print(f'Excluded {excluded_count} new file diff block(s) from patch')
                if excluded_files:
                    for f in excluded_files:
                        print(f'  - {f}')
            
            # Debug: save temp file if requested
            if args.debug:
                debug_file = temp_patch_file + '.debug'
                import shutil
                shutil.copy2(temp_patch_file, debug_file)
                print(f'Debug: Saved temp patch file to: {debug_file}')
        else:
            if args.verbose:
                print('Patch only contains new files (no existing files to modify)')
                print(f'Excluded {excluded_count} new file diff block(s) from patch')
                if excluded_files:
                    for f in excluded_files:
                        print(f'  - {f}')

    print(f"Applying patch '{patch_file}' to directory '{target_dir}'...")
    
    # Detect strip level BEFORE creating new files
    # This ensures new file paths match the rest of the patch application logic
    strip_level = detect_strip_level(patch_file, args.strip, args.verbose)
    
    try:
        # Create new files BEFORE applying patch
        if new_files:
            if args.verbose:
                print('\nCreating new files before applying patch...')
            # Use the detected strip_level instead of hardcoded 1
            created = create_new_files(new_files, target_dir, strip_level, args.verbose)
            if created > 0:
                print(f'Created {created} new file(s).')
        
        # Only apply patch if there's a patch file (not None)
        if actual_patch_file is None:
            # Patch only contains new files, no existing files to modify
            print('Patch applied successfully! (only new files created)')
            return_code = 0
        else:
            pset = patch.fromfile(actual_patch_file)
            if not pset:
                print('Error: Failed to parse patch file.')
                sys.exit(1)
            
            if args.verbose:
                if hasattr(pset, 'items'):
                    print(f'\nParsed {len(pset.items)} patch items (after excluding new files)')
                    for i, item in enumerate(pset.items):
                        source = to_str(getattr(item, 'source', 'N/A'))
                        target = to_str(getattr(item, 'target', 'N/A'))
                        print(f'  Item {i+1}: {source} -> {target}')
            
            # Try to apply the patch
            success = pset.apply(root=target_dir, strip=strip_level)
            
            if success:
                print('Patch applied successfully!')
                return_code = 0
            else:
                print('Patch application failed!')
                
                # Check which items failed
                if hasattr(pset, 'items') and pset.items:
                    failed_items = []
                    new_file_items = []
                    for i, item in enumerate(pset.items):
                        source = to_str(getattr(item, 'source', None))
                        target = to_str(getattr(item, 'target', None))
                        
                        # Check if this is a new file that should have been excluded
                        if source and (source == '/dev/null' or source.endswith('/dev/null')):
                            new_file_items.append((i+1, source, target))
                            continue
                        
                        # Check if source file exists
                        source_path = None
                        if source:
                            source_clean = source
                            if strip_level > 0 and source_clean.startswith('a/'):
                                source_clean = source_clean[2:]
                            if source_clean:
                                source_path = os.path.join(target_dir, source_clean)
                        
                        if source_path and not os.path.exists(source_path):
                            failed_items.append((i+1, source, target, f'Source file missing: {source_path}'))
                        elif source_path:
                            # File exists, but patch failed - might be content mismatch
                            failed_items.append((i+1, source, target, 'Content mismatch (file may already be patched or modified)'))
                    
                    if new_file_items:
                        print('\nWarning: Found new file items in patch (should have been excluded):')
                        for item_num, source, target in new_file_items:
                            print(f'  Item {item_num} ({source} -> {target})')
                    
                    if failed_items and args.verbose:
                        print('\nFailed items:')
                        for item_num, source, target, reason in failed_items[:10]:  # Show first 10
                            print(f'  Item {item_num} ({source} -> {target}): {reason}')
                        if len(failed_items) > 10:
                            print(f'  ... and {len(failed_items) - 10} more items')
                
                return_code = 1
            
    except Exception as e:
        print(f'Error applying patch: {e}')
        if args.verbose:
            import traceback
            traceback.print_exc()
        return_code = 1
    finally:
        # Clean up temporary file if created
        if temp_patch_file and os.path.exists(temp_patch_file):
            if not args.debug:
                os.unlink(temp_patch_file)
            else:
                print(f'Debug: Keeping temp patch file: {temp_patch_file}')
    
    sys.exit(return_code)

if __name__ == '__main__':
    main()

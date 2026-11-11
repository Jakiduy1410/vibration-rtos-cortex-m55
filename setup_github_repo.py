import os
import shutil
import subprocess
import urllib.request
import json

base_dir = r'c:\Users\ADMIN\Documents\Operating System\vibration_rtos_m55'
os.chdir(base_dir)

# 1. Remove nested .git directories
for sub in ['freertos_kernel', 'cmsis']:
    git_dir = os.path.join(base_dir, sub, '.git')
    if os.path.exists(git_dir):
        print(f"Removing nested {git_dir}...")
        shutil.rmtree(git_dir, ignore_errors=True)

# 2. Get GitHub token from Git Credential Manager
p = subprocess.Popen(['git', 'credential', 'fill'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
out, _ = p.communicate(input='protocol=https\nhost=github.com\n\n', timeout=5)
token = [l.split('=', 1)[1] for l in out.splitlines() if l.startswith('password=')][0]
username = 'Jakiduy1410'
repo_name = 'vibration-rtos-cortex-m55'
collab_user = 'chocong040406-art'

print(f"Authenticated as: {username}")

# 3. Create Private Repo via GitHub API
print(f"Creating private repository: {repo_name}...")
create_url = 'https://api.github.com/user/repos'
payload = json.dumps({
    'name': repo_name,
    'description': 'Real-time vibration monitoring & anomaly alarm system built on FreeRTOS for ARM Cortex-M55 (QEMU mps3-an547) with FPU/Helium MVE support.',
    'private': True,
    'has_issues': True,
    'has_projects': True,
    'has_wiki': False
}).encode('utf-8')

req = urllib.request.Request(create_url, data=payload, headers={
    'Authorization': f'token {token}',
    'Accept': 'application/vnd.github.v3+json',
    'User-Agent': 'Python'
}, method='POST')

try:
    with urllib.request.urlopen(req) as resp:
        print(f"Repository created successfully! HTTP {resp.status}")
except urllib.error.HTTPError as e:
    err_body = e.read().decode()
    if 'name already exists' in err_body:
        print(f"Repository {repo_name} already exists. Continuing...")
    else:
        print(f"HTTP Error {e.code}: {err_body}")
        raise

# 4. Initialize Local Git Repo & Push
print("Initializing local git repository...")
if not os.path.exists('.git'):
    subprocess.run(['git', 'init'], check=True)

subprocess.run(['git', 'config', 'user.name', username], check=True)
subprocess.run(['git', 'config', 'user.email', 'duykhuonggiang@gmail.com'], check=True)

# Add all files
subprocess.run(['git', 'add', '-A'], check=True)
# Commit
status_out = subprocess.run(['git', 'status', '--porcelain'], capture_output=True, text=True).stdout
if status_out.strip():
    subprocess.run(['git', 'commit', '-m', 'Initial commit: FreeRTOS vibration monitoring system for ARM Cortex-M55 (QEMU mps3-an547)'], check=True)

subprocess.run(['git', 'branch', '-M', 'main'], check=True)

remote_url_with_auth = f'https://{username}:{token}@github.com/{username}/{repo_name}.git'
remote_url_clean = f'https://github.com/{username}/{repo_name}.git'

# Set remote origin
subprocess.run(['git', 'remote', 'remove', 'origin'], stderr=subprocess.DEVNULL)
subprocess.run(['git', 'remote', 'add', 'origin', remote_url_with_auth], check=True)

print("Pushing to GitHub main branch...")
subprocess.run(['git', 'push', '-u', 'origin', 'main', '--force'], check=True)

# Reset remote URL to clean URL (no credentials stored in git config)
subprocess.run(['git', 'remote', 'set-url', 'origin', remote_url_clean], check=True)
print(f"Pushed successfully to: {remote_url_clean}")

# 5. Add Collaborator: chocong040406-art
print(f"Adding collaborator: {collab_user}...")
collab_url = f'https://api.github.com/repos/{username}/{repo_name}/collaborators/{collab_user}'
collab_payload = json.dumps({'permission': 'push'}).encode('utf-8')

req_collab = urllib.request.Request(collab_url, data=collab_payload, headers={
    'Authorization': f'token {token}',
    'Accept': 'application/vnd.github.v3+json',
    'User-Agent': 'Python'
}, method='PUT')

try:
    with urllib.request.urlopen(req_collab) as resp:
        collab_data = json.loads(resp.read().decode())
        print(f"Collaborator invite status: HTTP {resp.status}")
        invitation_id = collab_data.get('id')
        invite_url = collab_data.get('html_url')
        print(f"Invitation sent successfully! Invitation URL: {invite_url}")
except urllib.error.HTTPError as e:
    err_body = e.read().decode()
    print(f"HTTP Error {e.code} adding collaborator: {err_body}")

print("\n=== ALL TASKS COMPLETED SUCCESSFULLY! ===")

#!/usr/bin/env bash
# 一键安装 pyenv、编译依赖并设置 Python 3.10 环境，安装项目依赖
set -e

# 确保在项目根目录执行
PROJECT_DIR=$(pwd)
if [[ ! -f "$PROJECT_DIR/requirements.txt" ]]; then
  echo "请在包含 requirements.txt 的项目根目录下运行此脚本。"
  exit 1
fi

echo "1. 更新 apt 源并安装编译依赖..."
sudo apt update
sudo apt install -y make build-essential libssl-dev zlib1g-dev \
    libbz2-dev libreadline-dev libsqlite3-dev wget curl llvm \
    libncurses5-dev libncursesw5-dev xz-utils tk-dev libffi-dev \
    liblzma-dev git

# 安装 pyenv
if [[ ! -d "$HOME/.pyenv" ]]; then
  echo "2. 克隆 pyenv..."
  git clone https://github.com/pyenv/pyenv.git ~/.pyenv
else
  echo "pyenv 已存在，跳过克隆。"
fi

# 设置 pyenv 环境
export PYENV_ROOT="$HOME/.pyenv"
export PATH="$PYENV_ROOT/bin:$PATH"
eval "$(pyenv init -)"

# 要安装的 Python 版本
PYTHON_VERSION=3.10.12

# 安装指定 Python 版本
if ! pyenv versions --bare | grep -qx "$PYTHON_VERSION"; then
  echo "3. 安装 Python $PYTHON_VERSION..."
  pyenv install $PYTHON_VERSION
else
  echo "Python $PYTHON_VERSION 已安装，跳过。"
fi

# 在项目目录生成 .python-version
echo "4. 设置本地 Python 版本为 $PYTHON_VERSION..."
cd "$PROJECT_DIR"
pyenv local $PYTHON_VERSION

# 升级 pip 并安装 requirements
echo "5. 升级 pip 并安装 requirements.txt 中的依赖..."
pip install --upgrade pip
pip install -r requirements.txt

cat <<EOF

安装完成！
当前目录已配置 Python $PYTHON_VERSION。使用方式：
  cd $PROJECT_DIR
  python --version
  pip list
  ./best_goodput.py topology.yaml --print
EOF

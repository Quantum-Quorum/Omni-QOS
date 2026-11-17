# OmniQ-OS: Real-Time Quantum Linux
# System-wide Bash Configuration with Quantum Bioluminescent Theme

if [ -z "$OMNIQ_BASH_CONFIG_LOADED" ]; then
    export OMNIQ_BASH_CONFIG_LOADED=1

    export TERM=xterm-256color
    export COLORTERM=truecolor
    export EDITOR=nano
    export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
    export LD_LIBRARY_PATH=/usr/local/lib:/opt/omniq/lib:${LD_LIBRARY_PATH:-}

    # Two-Line Quantum Prompt (Zero multi-char emojis)
    if [ "${UID:-0}" -eq 0 ]; then
        PS1='\[\033[1;35m\]┌──(\[\033[1;36m\]omniq\[\033[1;35m\]@\[\033[1;32m\]qpu-core\[\033[1;35m\])-[\[\033[1;37m\]\w\[\033[1;35m\]]\n\[\033[1;35m\]└─\[\033[1;36m\]#\[\033[0m\] '
    else
        PS1='\[\033[1;35m\]┌──(\[\033[1;36m\]omniq\[\033[1;35m\]@\[\033[1;34m\]quantum\[\033[1;35m\])-[\[\033[1;37m\]\w\[\033[1;35m\]]\n\[\033[1;35m\]└─\[\033[1;32m\]$\[\033[0m\] '
    fi

    # Standard Aliases
    alias ls='ls --color=auto'
    alias ll='ls -lah --color=auto'
    alias la='ls -A --color=auto'
    alias grep='grep --color=auto'
    alias df='df -h'
    alias free='free -m'

    # OmniQ Quantum & OS Quick Commands
    alias smi='qpu-smi'
    alias cockpit='omniq-gui'
    alias hilbert='omniq-hilbert-space'
    alias circuits='omniq-circuit-maker'
    alias qec='omniq-qec'
    alias vqe='omniq-vqe-chem'
    alias qpu3d='omniq-qpu-3d'
    alias pqc='omniq-pqc-audit'
    alias files='omniq-files /root/circuits'
    alias status='omniq-ctl status'
    alias rtcheck='uname -a'

    if [ -t 1 ] && [ -z "$OMNIQ_BANNER_SHOWN" ]; then
        export OMNIQ_BANNER_SHOWN=1
        echo -e "\033[1;36m"
        cat << "EOF"
   ____                  _  ___           ____   _____
  / __ \ ____ ___   ____ (_)/ _ \         / __ \ / ___/
 / / / // __ `__ \ / __ \ // // / ______ / / / / \__ \
/ /_/ // / / / / // / / // // /_//_____// /_/ / ___/ /
\____//_/ /_/ /_//_/ /_//_/ \___/        \____/ /____/
EOF
        echo -e "\033[1;35m  OmniQ-OS 24.04-RT :: Hard Real-Time Quantum Operating System\033[0m"
        echo -e "\033[1;32m  [PREEMPT_RT Kernel | GPUDirect-QPU <600ns | QMMU Guard-Bands | ML-KEM-1024]\033[0m"
        echo -e "\033[0;37m  Commands: \033[1;36mqpu-smi\033[0;37m | \033[1;36momniq-gui\033[0;37m | \033[1;36momniq-circuit-maker\033[0;37m | \033[1;36momniq-hilbert-space\033[0m\n"
    fi
fi

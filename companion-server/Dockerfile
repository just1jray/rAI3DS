FROM oven/bun:1

# Install tmux for Claude Code session management
RUN apt-get update && apt-get install -y tmux && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY package.json bun.lockb* ./
RUN bun install

COPY . .

EXPOSE 3333 3334

CMD ["bun", "run", "dev"]

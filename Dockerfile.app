FROM python:3.11-slim

ENV PYTHONUNBUFFERED=1

WORKDIR /app

# Instala dependencias
COPY requirements.txt /app/requirements.txt
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
  && pip install --no-cache-dir -r /app/requirements.txt \
  && apt-get remove -y build-essential \
  && apt-get autoremove -y \
  && rm -rf /var/lib/apt/lists/*

# Copia todo el proyecto
COPY . /app

EXPOSE 5000

CMD ["python", "flask_app.py"]
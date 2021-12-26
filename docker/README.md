
# Docker Setup

To use docker, you need to install docker with gpu runtime enabled. For e.g., nvidia gpus require installation of `nvidia-container-toolkit`.


```
cd docker/
docker build -t aqua - < Dockerfile.ubuntu
docker run --rm -it --net=host --gpus all --entrypoint /aqua/build/aquagpuminer  aqua:latest
```
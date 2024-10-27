FROM eprosima/vulcanexus:jazzy-desktop 

RUN mkdir -p /ros2-ws/src
WORKDIR /ros2-ws/src

RUN apt update && \
    apt install -y x11-apps

RUN git clone --depth=1  https://github.com/eProsima/vulcanexus.git tmp_dir
    
RUN mv tmp_dir/code/turtlesim .
RUN rm -rf tmp_dir
WORKDIR /ros2-ws
RUN source /opt/vulcanexus/jazzy/setup.bash && \
    colcon build

ENTRYPOINT ["/bin/bash", "-c", "sleep infinity"] 


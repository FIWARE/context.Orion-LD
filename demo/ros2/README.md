# Demo ROS2 < - > NGSI-LD context Broker

The demo is structured in 2 directories:
  - `demo-dockerfile`
  - `demo-docker-compose`

## Make a precompiled docker image
This is convenient if you mean to run the demo more than once.
This is the reason the Dockerfile is needed.
To create the docker image with precompiled software, run:
```
cd demo-dockerfile
docker build -t ros2 .
```

After this, you have a Docker image with all the needed Ros2 components for the demo, including the TurtleSim.

Please, note that this image is a commodity image which makes running the software easier.

## Starting the demo
Before starting the demo, as X11 session owners, other users must be allowed to use the X Window System (to show the
turtles on screen).
For that, the following command is executed:

```
xhost local:root
```

Once that is done done (only needed the first time), the containers can be started:
```
cd demo-docker-compose
docker compose up -d 
```

When the dockers have started, connect to bash in the container of ros2:
```
docker exec -ti ros2 bash
```

### Turtles
Now the TurtleSim and the Keyboard controller can be started:

```
source /ros2-ws/install/setup.bash

# Show the turtles on the screen
ros2 run docs_turtlesim turtlesim_node_keys &

# Keyboard controller to move the turtles.
ros2 run docs_turtlesim turtlesim_multi_control 
```

# About the broker
The file `demo-docker-compose/config-dds.json` is mounted in Orion-LD's container.
This is the file that must be configured with the conversion from DDS topic to Entity ID+Tupe and Attribute Name, for Orion-LD.
When the docker is started, this file is used inside Orion-LD's container as its configuration file (`/root/.orionld`).
Orionld's docker is exporting port 1026 - It can be accessed from any terminal as `locahost:1026`


# Restart
Typically, the configuration file of Orion-LD will be updated and the whole thing needs to be restarted.
Kill the system like this:
```
cd demo-docker-compose
docker compose down
```
Then follow (again) the instructions of "Starting the demo"


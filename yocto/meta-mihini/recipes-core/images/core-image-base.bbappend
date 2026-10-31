# Add the Mihini data-store stack to the base image.
#
# mihini-agent is intentionally left out until its recipe is ported; uncomment
# it (and add the recipe's dependencies) once the vendored agent builds.

IMAGE_INSTALL:append = " mihini-schema mihini-datastore mihini-modules"
# IMAGE_INSTALL:append = " mihini-agent"

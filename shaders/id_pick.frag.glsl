#version 450 core

layout (location = 0) out int outId;

uniform int uEntityId;

void main()
{
    outId = uEntityId;
}

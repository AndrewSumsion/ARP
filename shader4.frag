#version 330 core

layout(location=0) out vec4 color;

uniform sampler2D tex;
in vec2 texCoord;

in vec3 interpolatedNormal;
in vec3 vertPos;

const float Ka = 1;   
const float Kd = 1; 
const float Ks = 1;  
const float shininess = 200;

const vec3 lightPos = vec3(0.0, -1.0, -1.0);
const vec3 specularColor = vec3(0.0, 0.0, 0.0);
const vec3 lightColor = vec3(1.0, 1.0, 1.0);


void main() {
  vec4 texColor = texture( tex, texCoord );
  vec3 diffuseColor = vec3( texColor );
  vec3 ambientColor = diffuseColor * 0.1;
  vec3 norms = normalize(interpolatedNormal);
  vec3 lightDirection = normalize(lightPos - vertPos);

  float geometryTerm = max(dot(norms, lightDirection), 0.0);

  vec3 reflectedLightVector = reflect(-lightDirection, norms);     
  vec3 viewVector = normalize(-vertPos); 

  float halfAngle = max(dot(reflectedLightVector, viewVector), 0.0);
  float specular = pow(halfAngle, shininess);
    //color = texture( tex, texCoord );
    color = vec4(Ka * ambientColor + Kd * max(geometryTerm, 0) * diffuseColor + Ks * max(specular, 0) * specularColor, 1.0);
} 
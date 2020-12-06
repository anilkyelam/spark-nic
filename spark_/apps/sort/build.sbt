name := "SparkSort"

version := "0.1"

resolvers += Resolver.mavenLocal        // publish spark jars to local maven with "sbt publishLocal"
scalaVersion := "2.12.10"               // got this from current spark build; at spark/build/scala-<version>


libraryDependencies += "org.apache.spark" %% "spark-core" % "3.1.0-SNAPSHOT"
libraryDependencies += "org.apache.spark" %% "spark-sql" % "3.1.0-SNAPSHOT"
// libraryDependencies += "com.google.guava" %% "guava" % "16.0" 


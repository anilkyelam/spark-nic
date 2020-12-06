/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package PowerMeasurements

import java.nio.charset.Charset
import java.util.Comparator

import com.google.common.primitives.UnsignedBytes
import org.apache.spark.{SparkConf, SparkContext}
import org.apache.spark.sql.SparkSession

import scala.collection.mutable.ArrayBuffer

/**
 * This is a great example program to stress test Spark's shuffle mechanism.
 *
 * See http://sortbenchmark.org/
 */
object TeraSort {

  implicit val caseInsensitiveOrdering : Comparator[Array[Byte]] =
    UnsignedBytes.lexicographicalComparator

  def main(args: Array[String]) {

    if (args.length < 2) {
      println("Requires at least two arguments, the SparkMaster and Input size or file path")
      System.exit(0)
    }

    val conf = new SparkConf().
      setMaster(args(0)).
      setAppName("TeraSort")

    val sc = new SparkContext(conf)

    val mySpark = SparkSession
      .builder()
      .appName("TeraSort")
      .config(conf)
      .getOrCreate()
    import mySpark.implicits._

    // // Processing input from command line arguments args(1)
    // var input_path:String = null
    // try {
    //   val input_size_gb = args(1).toInt/1000
    //   if (input_size_gb > 300 || input_size_gb % 100 != 0){
    //     println("If specifying input size in gb, provide a multiple of 100gb and make sure it is not above 300gb!")
    //     System.exit(-1)
    //   }

    //   val num_parts = input_size_gb * 2 / 25    // Each part is 12.5 GB
    //   var i : Int = 0
    //   input_path = "/user/ayelam/sort_inputs/part_%d.input".format(i)
    //   while (i < num_parts-1){
    //     i += 1
    //     input_path += ",/user/ayelam/sort_inputs/part_%d.input".format(i)
    //   }
    // } catch {
    //   // If argument is not a number, consider it a path to input file or folder
    //   case e: NumberFormatException => input_path = args(1)
    // }
    var input_path = args(1)
    println("Loading input files from path(s): " + input_path)


    // If cmd line arguments 2 and 3 exist, then use them to set record size and number of partitions in final RDD
    // Make sure the record size perfectly divides the input, otherwise EOF exception will result
    var record_size_bytes: Int = 100
    var final_partition_count: Int = -1
    if (args.length >= 4){
      record_size_bytes = args(2).toInt
      final_partition_count = args(3).toInt
    }

    // Read in the input
    val dataset = sc.newAPIHadoopFile[Array[Byte], Array[Byte], TeraInputFormat](input_path)
    dataset.setName("InputRDD")

    // Partition and sort the input with chosen number of final partitions
    if (final_partition_count == -1)  final_partition_count = dataset.partitions.length
    val sorted = dataset.repartitionAndSortWithinPartitions(new TeraSortPartitioner(final_partition_count))
    sorted.setName("SortedRDD")

    var count = sorted.count()
    println("Terasort output partitions count: " + final_partition_count)
    println("Terasort output records count: " + count)

    // To keep the spark job alive for checking things on Web UI
    // Thread.sleep(60000)

    sc.stop()
  }
}

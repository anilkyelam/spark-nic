package PowerMeasurements

import org.apache.spark.SparkFiles
import org.apache.spark.rdd.RDD
import org.apache.spark.SparkContext
import org.apache.spark.SparkConf


// sbt build issues: https://stackoverflow.com/questions/45531198/warnings-while-building-scala-spark-project-with-sbt
/**
  * Test sparkMeasure (https://github.com/LucaCanali/sparkMeasure). Use:
  * bin/spark-submit --packages ch.cern.sparkmeasure:spark-measure_2.11:0.13 \
  * --class ch.cern.testSparkMeasure.testSparkMeasure <path>/testsparkmeasurescala_2.11-0.1.jar
  */
object SortLegacy {

  def main(args: Array[String]): Unit = {
    val inputPath = args(1)
    val outputPath = args(2)

    val cl = ClassLoader.getSystemClassLoader
    cl.asInstanceOf[java.net.URLClassLoader].getURLs.take(20).foreach(println)

    val conf = new SparkConf().
      setMaster(args(0)).
      setAppName("Sort")
    val sc = new SparkContext(conf)

    //https://stackoverflow.com/questions/7109943/how-to-define-orderingarraybyte
    implicit val ordering = new math.Ordering[Array[Byte]] {
      def compare(a: Array[Byte], b: Array[Byte]): Int = {
        if (a eq null) {
          if (b eq null) 0
          else -1
        }
        else if (b eq null) 1
        else {
          val L = math.min(a.length, b.length)
          var i = 0
          while (i < L) {
            if (a(i) < b(i)) return -1
            else if (b(i) < a(i)) return 1
            i += 1
          }
          if (L < b.length) -1
          else if (L < a.length) 1
          else 0
        }
      }
    }

    println("===================== Spark Conf: Begin ==========================")
    println(conf.toDebugString)
    println("===================== Spark Conf: End ==========================")

    // val taskMetrics = ch.cern.sparkmeasure.TaskMetrics(spark)
    // taskMetrics.begin()

    var text_RDD: RDD[Array[Byte]] = sc.binaryRecords(inputPath,100)
    var kv_RDD: RDD[(Array[Byte],Array[Byte])] = text_RDD.map(line => (line.slice(0,10), line.slice(10,100)))
    text_RDD.unpersist()
    text_RDD=null
    // on aws
    var count = kv_RDD.sortBy(_._1, true).saveAsObjectFile(outputPath)
    println("Spark output count: " + count)
  }
}

package PowerMeasurements

import org.apache.spark.SparkFiles
import org.apache.spark.rdd.RDD
import org.apache.spark.SparkContext
import org.apache.spark.SparkConf


// sbt build issues: https://stackoverflow.com/questions/45531198/warnings-while-building-scala-spark-project-with-sbt
/**
  Sort
  1. Does not write output
  2. Scales value of each input record to required factor to allow testing with bigger input records
  */
object SortNoDiskScaledInput {

  def main(args: Array[String]): Unit = {
    val inputPath = args(1)
    val value_scale_factor = args(2).toInt

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
    var kv_RDD: RDD[(Array[Byte],Array[Byte])] = text_RDD.map(line => (line.slice(0,10), scale_array(line, value_scale_factor)))
    text_RDD.unpersist()
    text_RDD=null

    // Get size of all values, just to double-check
    // var size = kv_RDD.aggregate(0)((acc, inp) => acc + inp._2.length, (a1, a2) => a1 + a2)
    // println("Spark value size: " + size)

    // Do not write output to disk
    var count = kv_RDD.sortBy(_._1, true).count()
    println("Spark output count: " + count)
  }

  def scale_array(a: Array[Byte], i: Int): Array[Byte] = {
    var result = a
    for(j <- 2 to i){
      result = result ++ a
    }
    result
  }

}

